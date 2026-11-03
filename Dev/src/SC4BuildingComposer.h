#pragma once

#include "Hooks/CallSiteHook.h"
#include "SC4BakedComposite.h"
#include "SC4BuildingShapeCatalog.h"
#include "cGZPersistResourceKey.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class cIS3DModelInstance;

/// <summary>
/// Draws baked models as complete buildings at camera angles the game has no picture for. A
/// baked model is a low-detail shape with one picture painted across it from one direction, so
/// from any other direction its walls stretch, its far side is missing, and parts of the
/// picture land on the wrong surface. The game ships four such pictures of each model, one for
/// each quarter turn. This class combines them, using each one only for the surfaces it saw
/// well. Native angles are left untouched, so Classic mode and region previews stay
/// pixel-identical. Buildings listed in building-shapes.json have their pictures painted onto
/// their true shape instead of the game's low-detail one. All members run on the game thread.
/// </summary>
class SC4BuildingComposer
{
public:
	// Creates a composer holding nothing.
	SC4BuildingComposer();
	// Destroys the composer. Shutdown must already have run.
	~SC4BuildingComposer();

	/// <summary>
	/// Redirects the game's model drawing through this object and reads building-shapes.json.
	/// Run when a city finishes loading.
	/// </summary>
	/// <returns>True when the redirect is in place.</returns>
	bool Install();
	/// <summary>
	/// Restores the game's model drawing and releases every model held. Run before the city or
	/// the game's services shut down.
	/// </summary>
	void Shutdown();
	/// <summary>
	/// Restores the game's model drawing without touching any game object. Run on an abortive
	/// quit, when the game's services can no longer be trusted but this code may be unloaded.
	/// </summary>
	void RemoveHook();
	/// <summary>
	/// Sets up one frame. Run once per camera frame, before the draw system.
	/// </summary>
	/// <param name="viewIsNative">Whether the camera is at the current zoom stage's native angles.</param>
	/// <param name="zoom">Current native zoom stage.</param>
	/// <param name="viewMoved">Whether the view changed since the last frame, which redraws everything on screen.</param>
	/// <returns>True when a full redraw is needed to show newly finished models.</returns>
	bool BeginFrame(bool viewIsNative, int32_t zoom, bool viewMoved);
	/// <summary>
	/// Describes the current work for the camera diagnostics.
	/// </summary>
	/// <returns>A one-line summary.</returns>
	std::string GetStatus() const;

private:
	// The native description of one drawable mesh, from 0x7D4A80.
	struct NativeMesh
	{
		// Vertex layout, count, and data.
		void* vertices;
		// Index data, or null to draw vertices in order.
		void* indices;
		// Ranges of indices to draw.
		void* primitives;
	};

	// One finished mesh of a composite, with the native records the draw call reads.
	struct DrawableMesh;

	// Stands in for the native model while drawing. Its slots match those 0x7D5710 calls on the
	// native model (0xABD554): 3 returns the mesh count, 4 a mesh, and 6 a material.
	class Composite
	{
	public:
		// Unused native slot.
		virtual void Slot0();
		// Unused native slot.
		virtual void Slot1();
		// Unused native slot.
		virtual void Slot2();
		/// <summary>
		/// Returns the number of meshes to draw.
		/// </summary>
		/// <returns>The mesh count.</returns>
		virtual uint32_t GetMeshCount() const;
		/// <summary>
		/// Returns one mesh to draw.
		/// </summary>
		/// <param name="index">Mesh index.</param>
		/// <returns>The native mesh record.</returns>
		virtual const NativeMesh* GetMesh(uint32_t index) const;
		// Unused native slot.
		virtual void Slot5();
		/// <summary>
		/// Returns the material of one mesh.
		/// </summary>
		/// <param name="index">Mesh index.</param>
		/// <returns>The native material.</returns>
		virtual void* GetMaterial(uint32_t index) const;

		// Meshes in draw order.
		std::vector<std::unique_ptr<DrawableMesh>> meshes;
	};

	// The four rotation variants of a model at one zoom stage.
	struct Family
	{
		// Key of variant 0.
		cGZPersistResourceKey baseKey;
		// Zoom stage of the model.
		int32_t zoom = 0;
		// Next variant to load, or 4 when all are loaded.
		int32_t nextVariant = 0;
		// Whether the model turned out not to be usable. It then draws natively.
		bool unsupported = false;
		// The building's true shape from the catalog, or null to keep the game's shape.
		const SC4BuildingShape* shape = nullptr;
		// View time of the last draw that wanted this family.
		uint64_t lastUsedTick = 0;
		// A reference to each variant, which keeps its meshes and textures loaded.
		std::array<cIS3DModelInstance*, 4> instances{};
		// Native materials of each variant's meshes.
		std::array<std::vector<void*>, 4> materials;
		// Each variant, clipped to what its camera saw.
		std::array<SC4BakedModel, 4> clipped;
		// Finished composite for each current variant, built on first use.
		std::array<std::unique_ptr<Composite>, 4> composites;
		// Whether each composite has been asked for.
		std::array<bool, 4> composeQueued{};
		// Models that lead here, each holding a reference so its address stays unique.
		std::vector<void*> resources;
	};

	// A composite waiting to be built.
	struct ComposeRequest
	{
		// Family with every variant loaded.
		Family* family;
		// Variant whose model space the composite uses.
		int32_t variant;
	};

	// Where a loaded model resource leads.
	struct ResourceEntry
	{
		// Family of the model, or null when it always draws natively.
		Family* family = nullptr;
		// Which variant of the family it is.
		int32_t variant = 0;
	};

	/// <summary>
	/// Replaces the native draw of one model. Binary-verified signature: thiscall on the draw
	/// context, popping four arguments (0x7D5710).
	/// </summary>
	/// <param name="context">Native draw context.</param>
	/// <param name="unusedEdx">Unused register argument of the fastcall form.</param>
	/// <param name="instance">Native model instance.</param>
	/// <param name="geometry">Native object that lists the meshes.</param>
	/// <param name="attributes">Native draw attributes.</param>
	/// <param name="flag">Native draw flag.</param>
	static void __fastcall DrawModelHook(void* context, void* unusedEdx, void* instance, void* geometry, void* attributes, uint32_t flag);

	/// <summary>
	/// Returns the composite to draw in place of a native model.
	/// </summary>
	/// <param name="instance">Native model instance.</param>
	/// <param name="geometry">Native object that lists the meshes.</param>
	/// <returns>The composite, or null to draw natively.</returns>
	Composite* FindComposite(void* instance, void* geometry);
	/// <summary>
	/// Records a model resource seen for the first time.
	/// </summary>
	/// <param name="instance">Native model instance drawing it.</param>
	/// <param name="resource">Native model resource.</param>
	/// <returns>The record for it.</returns>
	ResourceEntry& AddResource(void* instance, void* resource);
	/// <summary>
	/// Loads and clips the next variant of a family.
	/// </summary>
	/// <param name="family">Family to work on.</param>
	void LoadNextVariant(Family& family);
	/// <summary>
	/// Builds the composite for a family's variant.
	/// </summary>
	/// <param name="family">Family with every variant loaded.</param>
	/// <param name="variant">Variant whose model space the composite uses.</param>
	void BuildComposite(Family& family, int32_t variant);
	/// <summary>
	/// Releases what a family holds and forgets it.
	/// </summary>
	/// <param name="family">Family to release.</param>
	void ReleaseFamily(Family& family);
	/// <summary>
	/// Releases families no draw has wanted for a while.
	/// </summary>
	void ReleaseUnusedFamilies();
	/// <summary>
	/// Reads building-shapes.json into the catalog. A missing file leaves the catalog empty.
	/// </summary>
	void LoadShapes();

	// Redirect of the native model draw.
	CallSiteHook drawHook;
	// True shapes of the buildings listed in building-shapes.json.
	SC4BuildingShapeCatalog shapes;
	// Whether composites replace native models this frame.
	bool active = false;
	// Zoom stage of the current frame.
	int32_t currentZoom = 0;
	// Tick of the current frame.
	uint64_t currentTick = 0;
	// Milliseconds the view has spent moving. Families age by this clock, since only a moving view
	// redraws every model on screen.
	uint64_t viewTick = 0;
	// Tick of the last pass that looked for unused families.
	uint64_t lastSweepTick = 0;
	// Tick of the last redraw requested for finished models.
	uint64_t lastRedrawTick = 0;
	// Whether a model has finished since the last redraw request.
	bool redrawWanted = false;
	// Records of every model resource seen, by address.
	std::unordered_map<void*, ResourceEntry> resources;
	// Families by the group and instance of their variant 0.
	std::unordered_map<uint64_t, std::unique_ptr<Family>> families;
	// Families waiting for their variants to load.
	std::deque<Family*> loadQueue;
	// Composites waiting to be built.
	std::deque<ComposeRequest> composeQueue;
	// Composite draws in the last frame.
	uint32_t compositeDraws = 0;
	// Native draws of plain models in the last frame.
	uint32_t nativeDraws = 0;
	// Composite draws so far in this frame.
	uint32_t frameCompositeDraws = 0;
	// Native draws so far in this frame.
	uint32_t frameNativeDraws = 0;
	// Total time spent loading and clipping, in milliseconds.
	double buildMilliseconds = 0.0;
	// Number of families finished.
	uint32_t familiesFinished = 0;
	// Number of families found unusable.
	uint32_t familiesUnsupported = 0;
};
