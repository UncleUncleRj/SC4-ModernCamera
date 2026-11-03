#include "SC4BuildingComposer.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "PluginPaths.h"
#include "cIGZPersistResource.h"
#include "cIGZPersistResourceManager.h"
#include "cIS3DModelInstance.h"
#include "cISC4Occupant.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

// The interface the game loads models through. The SDK only forward-declares this class.
// Binary-verified on the 641 executable: vtable 0xABD608, reached by the resource manager with
// interface 0x9AD0E7E3 at 0x48FA12 and used by the model maker at 0x48FA26. Only the slots this
// file needs are declared.
class cIS3DResModel : public cIGZUnknown
{
public:
	// Slot 3 (0x520080). Not used here.
	virtual void Unused3() = 0;
	// Slot 4 (0x804150): creates an animated instance. Not used here.
	virtual void Unused4() = 0;
	// Slot 5 (0x8040E0): creates a plain instance of one frame, adding a reference for the caller.
	virtual bool CreateInstance(uint32_t frame, cIS3DModelInstance** instance) = 0;
};

namespace
{
	// Call to the native model draw inside the model instance's Draw (0x801750). It is the only
	// call to that function, and every model instance class draws through it.
	constexpr uintptr_t kDrawModelCallSite = 0x00801779;
	// Native model draw (binary-verified, thiscall on the draw context, popping four arguments).
	constexpr uintptr_t kDrawModelAddress = 0x007D5710;
	// Vtable of the plain model instance's geometry object. Animated and network models have
	// other classes and keep drawing natively.
	constexpr uintptr_t kPlainModelVTable = 0x00ABD554;
	// Offset of the model resource in a plain model instance (binary-verified: 0x801C40).
	constexpr size_t kInstanceResourceOffset = 0x28;
	// Offset of the mesh list in a plain model instance (binary-verified: 0x801AD0).
	constexpr size_t kInstanceMeshesOffset = 0x04;
	// Offset of the material list in a plain model instance (binary-verified: 0x801EC0).
	constexpr size_t kInstanceMaterialsOffset = 0x10;
	// Size of one native material (binary-verified: 0x801EC0).
	constexpr size_t kMaterialSize = 0x34;
	// Offset of the texture count in a material (binary-verified: 0x7D5310).
	constexpr size_t kMaterialTextureCountOffset = 0x0B;
	// Offset of the first texture stage in a material (binary-verified: 0x7D5310).
	constexpr size_t kMaterialStagesOffset = 0x10;
	// Size of one texture stage in a material (binary-verified: 0x7D5310).
	constexpr size_t kMaterialStageSize = 0x14;
	// Offset of the magnification filter in a texture stage (binary-verified: 0x7D5310, 0x7D4280).
	constexpr size_t kStageMagnificationFilterOffset = 0x06;
	// Offset of the minification filter in a texture stage (binary-verified: 0x7D5310, 0x7D4280).
	constexpr size_t kStageMinificationFilterOffset = 0x07;
	// Filters the renderer's kForceBilinear property selects (binary-verified: 0x7D4280). At free
	// angles a picture is stretched across its surface, where point sampling shows every texel.
	constexpr uint8_t kBilinearMagnificationFilter = 1;
	constexpr uint8_t kBilinearMinificationFilter = 7;
	// Offset of the instance's cIS3DModelInstance interface (binary-verified: 0x801750, 0x8040E0).
	constexpr size_t kInstanceInterfaceOffset = 0xA0;
	// Interface the resource manager returns a model through (binary-verified: 0x48FA08).
	constexpr uint32_t kResModelInterfaceId = 0x9AD0E7E3;
	// Interface that gives a model resource's key (binary-verified: QueryInterface at 0x804090).
	constexpr uint32_t kPersistResourceInterfaceId = 0x456B8F1D;
	// Native primitive type of a triangle list, the only type in the shipped models.
	constexpr uint32_t kTriangleListType = 0;
	// Native vertex formats of position plus one texture coordinate. See SC4BakedVertex.
	constexpr uint32_t kBakedVertexFormatPacked = 0x80004001;
	constexpr uint32_t kBakedVertexFormatShort = 0x2;
	// Native pitch of each zoom stage in radians, from the tables in default-camera.md.
	constexpr float kBakePitchByZoom[5] = { 0.52359879f, 0.61086524f, 0.69813170f, 0.78539819f, 0.78539819f };
	// Native yaw in radians, which every shipped model was baked at.
	constexpr float kBakeYaw = -0.39269909f;
	// Native type of building occupants (binary-verified: the model maker's case at 0x494390).
	// Thin props and flora are baked on boxes that hold them loosely, so several pictures of one
	// look worse than the single native one.
	constexpr uint32_t kBuildingOccupantType = 0x278128A0;
	// Lowest zoom stage drawn with composites. Farther stages draw each model too small to matter.
	constexpr int32_t kMinimumCompositeZoom = 3;
	// Largest error in a texture coordinate of a baked model. Half a texel of a small tile.
	constexpr float kPictureFitTolerance = 0.02f;
	// Time allowed for loading models per frame, in milliseconds.
	constexpr double kFrameBudgetMilliseconds = 3.0;
	// How long the view must move without drawing a family before it is released, in milliseconds.
	// A still view redraws only what changes, so time spent still says nothing about what is on screen.
	constexpr uint64_t kFamilyLifetimeMilliseconds = 30000;
	// Most view time one frame can add, in milliseconds, so a stall does not age every family at once.
	constexpr uint64_t kMaximumFrameAgeMilliseconds = 100;
	// Time between passes that look for unused families, in milliseconds.
	constexpr uint64_t kSweepIntervalMilliseconds = 2000;
	// Shortest time between redraws requested for finished models, in milliseconds.
	constexpr uint64_t kRedrawIntervalMilliseconds = 250;

	// Native vertex record, from 0x7D4A80.
	struct NativeVertices
	{
		uint32_t format;
		uint16_t flags;
		uint16_t count;
		const void* data;
	};

	// Native index record, from 0x7D4A80.
	struct NativeIndices
	{
		const uint16_t* data;
	};

	// One range of indices, from 0x7D4A80.
	struct NativePrimitive
	{
		uint32_t type;
		uint32_t first;
		uint32_t count;
	};

	// Native primitive list, from 0x7D4A80.
	struct NativePrimitives
	{
		uint32_t unused;
		const NativePrimitive* begin;
		const NativePrimitive* end;
	};

	// The composer the draw hook works for. One exists, owned by the camera controller.
	SC4BuildingComposer* gComposer = nullptr;
	// Native model draw.
	using DrawModelFunction = void(__thiscall*)(void* context, void* instance, void* geometry, void* attributes, uint32_t flag);
	DrawModelFunction gDrawModel = nullptr;

	// Returns a time in milliseconds for measuring short work.
	double GetMilliseconds()
	{
		LARGE_INTEGER frequency;
		LARGE_INTEGER counter;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&counter);
		return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	}

	// Returns a value read from a native object.
	template <typename Type>
	Type ReadField(const void* object, size_t offset)
	{
		return *reinterpret_cast<const Type*>(static_cast<const uint8_t*>(object) + offset);
	}
}

struct SC4BuildingComposer::DrawableMesh
{
	// Vertices, in the current variant's model space.
	std::vector<SC4BakedVertex> vertices;
	// Three vertex indices per triangle.
	std::vector<uint16_t> indices;
	// Native vertex record.
	NativeVertices nativeVertices{};
	// Native index record.
	NativeIndices nativeIndices{};
	// The one range of indices.
	NativePrimitive primitive{};
	// Native primitive list.
	NativePrimitives nativePrimitives{};
	// Native mesh record.
	NativeMesh nativeMesh{};
	// Copy of the native material of the source mesh, with smooth filtering.
	uint8_t material[kMaterialSize] = {};
};

SC4BuildingComposer::SC4BuildingComposer() = default;
SC4BuildingComposer::~SC4BuildingComposer() = default;

void SC4BuildingComposer::Composite::Slot0() {}
void SC4BuildingComposer::Composite::Slot1() {}
void SC4BuildingComposer::Composite::Slot2() {}
void SC4BuildingComposer::Composite::Slot5() {}

// Returns the number of meshes to draw.
uint32_t SC4BuildingComposer::Composite::GetMeshCount() const
{
	return static_cast<uint32_t>(meshes.size());
}

// Returns one mesh to draw.
const SC4BuildingComposer::NativeMesh* SC4BuildingComposer::Composite::GetMesh(uint32_t index) const
{
	return &meshes[index]->nativeMesh;
}

// Returns the material of one mesh.
void* SC4BuildingComposer::Composite::GetMaterial(uint32_t index) const
{
	return meshes[index]->material;
}

// Redirects the native model draw through this object.
bool SC4BuildingComposer::Install()
{
	if (drawHook.IsInstalled()) {
		return true;
	}

	gComposer = this;
	if (!drawHook.Install(
		kDrawModelCallSite,
		kDrawModelAddress,
		reinterpret_cast<void*>(&SC4BuildingComposer::DrawModelHook))) {
		gComposer = nullptr;
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Building Composer: the native model draw call was not as expected. Models draw natively.");
		return false;
	}

	gDrawModel = reinterpret_cast<DrawModelFunction>(drawHook.GetOriginal());
	Logger::GetInstance().WriteLine(LogLevel::Info, "Building Composer: installed.");
	// Families point into the catalog, so it is only replaced while none exist.
	if (families.empty()) {
		LoadShapes();
	}
	return true;
}

// Restores the native draw and releases everything held.
void SC4BuildingComposer::Shutdown()
{
	active = false;
	drawHook.Uninstall();
	loadQueue.clear();
	composeQueue.clear();
	for (auto& [instance, family] : families) {
		ReleaseFamily(*family);
	}
	families.clear();
	for (auto& [resource, entry] : resources) {
		if (!entry.family) {
			static_cast<cIGZUnknown*>(resource)->Release();
		}
	}
	resources.clear();
	redrawWanted = false;
}

// Restores the native draw and leaves every game object alone.
void SC4BuildingComposer::RemoveHook()
{
	active = false;
	drawHook.Uninstall();
}

// Decides whether composites apply this frame and advances the loading work.
bool SC4BuildingComposer::BeginFrame(bool viewIsNative, int32_t zoom, bool viewMoved)
{
	if (!drawHook.IsInstalled()) {
		return false;
	}

	active = !viewIsNative && zoom >= kMinimumCompositeZoom;
	currentZoom = zoom;
	const uint64_t now = GetTickCount64();
	if (viewMoved && currentTick != 0) {
		viewTick += (std::min)(now - currentTick, kMaximumFrameAgeMilliseconds);
	}
	currentTick = now;
	compositeDraws = frameCompositeDraws;
	nativeDraws = frameNativeDraws;
	frameCompositeDraws = 0;
	frameNativeDraws = 0;

	const double started = GetMilliseconds();
	while (!composeQueue.empty() && GetMilliseconds() - started < kFrameBudgetMilliseconds) {
		const ComposeRequest request = composeQueue.front();
		composeQueue.pop_front();
		BuildComposite(*request.family, request.variant);
		redrawWanted = true;
	}
	while (!loadQueue.empty() && GetMilliseconds() - started < kFrameBudgetMilliseconds) {
		Family& family = *loadQueue.front();
		LoadNextVariant(family);
		if (family.unsupported || family.nextVariant >= 4) {
			loadQueue.pop_front();
			if (family.unsupported) {
				++familiesUnsupported;
			}
			else {
				++familiesFinished;
				redrawWanted = true;
			}
		}
	}
	buildMilliseconds += GetMilliseconds() - started;

	if (currentTick - lastSweepTick >= kSweepIntervalMilliseconds) {
		lastSweepTick = currentTick;
		ReleaseUnusedFamilies();
	}

	const bool workDone = composeQueue.empty() && loadQueue.empty();
	if (redrawWanted && active && (workDone || currentTick - lastRedrawTick >= kRedrawIntervalMilliseconds)) {
		redrawWanted = false;
		lastRedrawTick = currentTick;
		return true;
	}
	return false;
}

// Describes the current work for the camera diagnostics.
std::string SC4BuildingComposer::GetStatus() const
{
	char buffer[200];
	std::snprintf(
		buffer,
		sizeof(buffer),
		"active=%d shapes=%u families=%u finished=%u unsupported=%u queued=%u frameDraws=%u/%u buildMs=%.1f",
		active ? 1 : 0,
		static_cast<unsigned>(shapes.GetCount()),
		static_cast<unsigned>(families.size()),
		familiesFinished,
		familiesUnsupported,
		static_cast<unsigned>(loadQueue.size()),
		compositeDraws,
		compositeDraws + nativeDraws,
		buildMilliseconds);
	return buffer;
}

// Draws a composite in place of the native model when one is ready.
void __fastcall SC4BuildingComposer::DrawModelHook(void* context, void*, void* instance, void* geometry, void* attributes, uint32_t flag)
{
	SC4BuildingComposer* composer = gComposer;
	if (composer && composer->active && ReadField<uintptr_t>(geometry, 0) == kPlainModelVTable) {
		if (Composite* composite = composer->FindComposite(instance, geometry)) {
			++composer->frameCompositeDraws;
			gDrawModel(context, instance, composite, attributes, flag);
			return;
		}
		++composer->frameNativeDraws;
	}
	gDrawModel(context, instance, geometry, attributes, flag);
}

// Returns the composite for a native model, starting its loading when it has none yet.
SC4BuildingComposer::Composite* SC4BuildingComposer::FindComposite(void* instance, void* geometry)
{
	void* resource = ReadField<void*>(geometry, kInstanceResourceOffset);
	if (!resource) {
		return nullptr;
	}

	auto found = resources.find(resource);
	ResourceEntry& entry = found != resources.end() ? found->second : AddResource(instance, resource);
	Family* family = entry.family;
	if (!family) {
		return nullptr;
	}

	family->lastUsedTick = viewTick;
	if (family->unsupported || family->nextVariant < 4) {
		return nullptr;
	}

	std::unique_ptr<Composite>& composite = family->composites[static_cast<size_t>(entry.variant)];
	if (!composite && !family->composeQueued[static_cast<size_t>(entry.variant)]) {
		family->composeQueued[static_cast<size_t>(entry.variant)] = true;
		composeQueue.push_back({ family, entry.variant });
	}
	return composite.get();
}

// Finds the family of a model resource from its key, creating and queueing a new one.
SC4BuildingComposer::ResourceEntry& SC4BuildingComposer::AddResource(void* instance, void* resource)
{
	auto* unknown = static_cast<cIGZUnknown*>(resource);
	unknown->AddRef();
	ResourceEntry& entry = resources[resource];

	cIGZUnknown* owner = static_cast<cIS3DModelInstance*>(instance)->GetOwner();
	cISC4Occupant* occupant = nullptr;
	if (!owner || !owner->QueryInterface(GZIID_cISC4Occupant, reinterpret_cast<void**>(&occupant)) || !occupant) {
		return entry;
	}
	const uint32_t occupantType = static_cast<uint32_t>(occupant->GetType());
	occupant->Release();
	if (occupantType != kBuildingOccupantType) {
		return entry;
	}

	cIGZPersistResource* persist = nullptr;
	if (!unknown->QueryInterface(kPersistResourceInterfaceId, reinterpret_cast<void**>(&persist)) || !persist) {
		return entry;
	}
	cGZPersistResourceKey key;
	persist->GetKey(key);
	persist->Release();

	// The rotation variants of a model at one zoom stage differ in these bits of the instance.
	const int32_t zoom = static_cast<int32_t>((key.instance >> 8) & 0xF);
	const int32_t variant = static_cast<int32_t>((key.instance >> 4) & 0xF);
	if (zoom > 4 || variant > 3 || zoom < kMinimumCompositeZoom) {
		return entry;
	}

	const uint32_t baseInstance = key.instance - static_cast<uint32_t>(variant) * 0x10;
	std::unique_ptr<Family>& slot = families[(static_cast<uint64_t>(key.group) << 32) | baseInstance];
	if (!slot) {
		slot = std::make_unique<Family>();
		slot->baseKey = cGZPersistResourceKey(key.type, key.group, baseInstance);
		slot->zoom = zoom;
		slot->shape = shapes.Find(key.group, key.instance);
		slot->lastUsedTick = viewTick;
		loadQueue.push_back(slot.get());
	}
	slot->resources.push_back(resource);
	entry.family = slot.get();
	entry.variant = variant;
	return entry;
}

// Loads the next variant of a family, reads its meshes, and clips them.
void SC4BuildingComposer::LoadNextVariant(Family& family)
{
	const int32_t variant = family.nextVariant;
	cGZPersistResourceKey key = family.baseKey;
	key.instance += static_cast<uint32_t>(variant) * 0x10;

	cIGZPersistResourceManagerPtr resourceManager;
	if (!resourceManager || !resourceManager->TestForKey(key)) {
		family.unsupported = true;
		return;
	}

	cIS3DResModel* model = nullptr;
	if (!resourceManager->GetResource(key, kResModelInterfaceId, reinterpret_cast<void**>(&model), 0, nullptr) || !model) {
		family.unsupported = true;
		return;
	}

	cIS3DModelInstance* instance = nullptr;
	const bool created = model->CreateInstance(0, &instance);
	model->Release();
	if (!created || !instance) {
		family.unsupported = true;
		return;
	}
	family.instances[static_cast<size_t>(variant)] = instance;

	const void* geometry = reinterpret_cast<const uint8_t*>(instance) - kInstanceInterfaceOffset;
	if (ReadField<uintptr_t>(geometry, 0) != kPlainModelVTable) {
		family.unsupported = true;
		return;
	}

	const SC4CameraAxes bake = GetCameraAxes(0, kBakeYaw, kBakePitchByZoom[family.zoom]);
	const auto* meshBegin = ReadField<const NativeMesh*>(geometry, kInstanceMeshesOffset);
	const auto* meshEnd = ReadField<const NativeMesh*>(geometry, kInstanceMeshesOffset + sizeof(void*));
	auto* materials = ReadField<uint8_t*>(geometry, kInstanceMaterialsOffset);
	SC4BakedModel& clipped = family.clipped[static_cast<size_t>(variant)];
	std::vector<void*>& materialList = family.materials[static_cast<size_t>(variant)];
	std::vector<SC4BakeMapping> mappings;

	for (const NativeMesh* mesh = meshBegin; mesh != meshEnd; ++mesh) {
		const auto* vertices = static_cast<const NativeVertices*>(mesh->vertices);
		const auto* indices = static_cast<const NativeIndices*>(mesh->indices);
		const auto* primitives = static_cast<const NativePrimitives*>(mesh->primitives);
		materialList.push_back(materials + kMaterialSize * materialList.size());

		SC4BakedMesh baked;
		SC4BakeMapping mapping;
		if (vertices && vertices->count != 0 && indices && indices->data && primitives) {
			if ((vertices->format != kBakedVertexFormatPacked && vertices->format != kBakedVertexFormatShort)
				|| !vertices->data) {
				family.unsupported = true;
				return;
			}

			const auto* data = static_cast<const SC4BakedVertex*>(vertices->data);
			baked.vertices.assign(data, data + vertices->count);
			for (const NativePrimitive* primitive = primitives->begin; primitive != primitives->end; ++primitive) {
				if (primitive->type != kTriangleListType) {
					family.unsupported = true;
					return;
				}
				baked.indices.insert(
					baked.indices.end(),
					indices->data + primitive->first,
					indices->data + primitive->first + primitive->count);
			}

			if (!FitBakeMapping(baked, bake, kPictureFitTolerance, mapping)) {
				family.unsupported = true;
				return;
			}
		}
		mappings.push_back(mapping);
		clipped.push_back(std::move(baked));
	}

	ClipToBakeView(clipped, bake);
	if (family.shape) {
		// The picture goes onto the building's true shape, wherever the camera saw that shape.
		SC4BakedModel shape;
		BuildShapeModel(*family.shape, variant, shape);
		ClipToBakeView(shape, bake);
		SC4BakedModel painted;
		PaintShape(clipped, mappings, shape, bake, painted);
		clipped.swap(painted);
	}
	++family.nextVariant;
}

// Builds the composite for one current variant and links it to the native records.
void SC4BuildingComposer::BuildComposite(Family& family, int32_t variant)
{
	const SC4CameraAxes bake = GetCameraAxes(0, kBakeYaw, kBakePitchByZoom[family.zoom]);
	std::vector<SC4CompositeMesh> composed;
	ComposeVariants(family.clipped, variant, bake, composed);

	auto composite = std::make_unique<Composite>();
	composite->meshes.reserve(composed.size());
	for (SC4CompositeMesh& source : composed) {
		// The vertex format of the source mesh is read back so the draw matches its layout.
		const void* geometry = reinterpret_cast<const uint8_t*>(family.instances[static_cast<size_t>(source.variant)]) - kInstanceInterfaceOffset;
		const auto* meshBegin = ReadField<const NativeMesh*>(geometry, kInstanceMeshesOffset);
		const auto* sourceVertices = static_cast<const NativeVertices*>(meshBegin[source.meshIndex].vertices);

		auto mesh = std::make_unique<DrawableMesh>();
		mesh->vertices = std::move(source.vertices);
		mesh->indices = std::move(source.indices);
		mesh->nativeVertices.format = sourceVertices->format;
		mesh->nativeVertices.flags = sourceVertices->flags;
		mesh->nativeVertices.count = static_cast<uint16_t>(mesh->vertices.size());
		mesh->nativeVertices.data = mesh->vertices.data();
		mesh->nativeIndices.data = mesh->indices.data();
		mesh->primitive.type = kTriangleListType;
		mesh->primitive.first = 0;
		mesh->primitive.count = static_cast<uint32_t>(mesh->indices.size());
		mesh->nativePrimitives.begin = &mesh->primitive;
		mesh->nativePrimitives.end = &mesh->primitive + 1;
		mesh->nativeMesh.vertices = &mesh->nativeVertices;
		mesh->nativeMesh.indices = &mesh->nativeIndices;
		mesh->nativeMesh.primitives = &mesh->nativePrimitives;
		std::memcpy(mesh->material, family.materials[static_cast<size_t>(source.variant)][source.meshIndex], kMaterialSize);
		const size_t stageCount = (std::min<size_t>)(mesh->material[kMaterialTextureCountOffset], 2);
		for (size_t stage = 0; stage < stageCount; ++stage) {
			uint8_t* filters = mesh->material + kMaterialStagesOffset + kMaterialStageSize * stage;
			filters[kStageMagnificationFilterOffset] = kBilinearMagnificationFilter;
			filters[kStageMinificationFilterOffset] = kBilinearMinificationFilter;
		}
		composite->meshes.push_back(std::move(mesh));
	}
	family.composites[static_cast<size_t>(variant)] = std::move(composite);
}

// Releases a family's references and removes its resource records.
void SC4BuildingComposer::ReleaseFamily(Family& family)
{
	for (cIS3DModelInstance*& instance : family.instances) {
		if (instance) {
			instance->Release();
			instance = nullptr;
		}
	}
	for (void* resource : family.resources) {
		resources.erase(resource);
		static_cast<cIGZUnknown*>(resource)->Release();
	}
	family.resources.clear();
}

// Reads the building shapes file. Problems are logged and the entries they affect are skipped.
void SC4BuildingComposer::LoadShapes()
{
	shapes.Clear();
	std::ifstream file(PluginPaths::GetBuildingShapesPath(), std::ios::binary);
	if (!file) {
		Logger::GetInstance().WriteLine(LogLevel::Info, "Building Composer: no building-shapes.json; every building keeps its game shape.");
		return;
	}

	const std::string text{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
	std::vector<std::string> problems;
	shapes.Load(text, problems);
	for (const std::string& problem : problems) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Building Composer: building-shapes.json: " + problem);
	}
	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"Building Composer: loaded " + std::to_string(shapes.GetCount()) + " building shapes.");
}

// Releases families that no draw has wanted for a while, which frees their textures.
void SC4BuildingComposer::ReleaseUnusedFamilies()
{
	for (auto it = families.begin(); it != families.end();) {
		Family& family = *it->second;
		const bool waiting = std::find(loadQueue.begin(), loadQueue.end(), &family) != loadQueue.end()
			|| std::any_of(composeQueue.begin(), composeQueue.end(), [&](const ComposeRequest& request) { return request.family == &family; });
		if (!waiting && viewTick - family.lastUsedTick >= kFamilyLifetimeMilliseconds) {
			ReleaseFamily(family);
			it = families.erase(it);
		}
		else {
			++it;
		}
	}
}
