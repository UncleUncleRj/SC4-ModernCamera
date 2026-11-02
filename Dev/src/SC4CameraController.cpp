#include "SC4CameraController.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "SC4GameConstants.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cISC43DRender.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cRZBaseString.h"
#include "cRZMessage2Standard.h"
#include "cS3DCamera.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace
{
	// Native update mode that recomputes the camera and scroll polygon without any messages.
	constexpr uint32_t kCameraSilentUpdateMode = 1;
	// Native update mode that sends the zoom/rotation-change messages and CameraUpdated, then clamps the target.
	constexpr uint32_t kCameraBroadcastUpdateMode = 2;
	// Native message that makes the renderer recalculate its view focus after the camera moves.
	constexpr uint32_t kSC4MessageCameraUpdated = 0x4A9F2FFE;
	// Cursor-text identifier reserved for the camera-dump confirmation.
	constexpr uint32_t kCameraDumpCursorTextId = 0x3D0C4D01;
	// Cursor-text priority that keeps the dump confirmation visible.
	constexpr uint32_t kCameraDumpCursorTextPriority = 1000;
	// Cursor-text identifier reserved for bookmark save and recall confirmations.
	constexpr uint32_t kCameraBookmarkCursorTextId = 0x3D0C4D02;
	// Cursor-text priority that keeps bookmark confirmations visible.
	constexpr uint32_t kCameraBookmarkCursorTextPriority = 1000;
	// Native camera-position update entry point for the supported executable.
	constexpr uintptr_t kUpdateCameraPositionAddress = 0x007CCF80;
	// Native custom-magnification entry point for the supported executable.
	constexpr uintptr_t kSetCustomMagnificationAddress = 0x007CD6E0;
	// Native routine that clamps the view target to the scroll polygon. Binary-verified: thiscall, no stack arguments.
	constexpr uintptr_t kClampViewTargetAddress = 0x007CD000;
	// Native routine that re-inserts every model into a new render grid. Binary-verified: thiscall on the
	// renderer, popping (previous zoom, previous rotation, zoom, rotation). It is the renderer's handler for a
	// rotation change, and it returns without working when the two rotations match.
	constexpr uintptr_t kRebuildRenderGridAddress = 0x007CAB30;
	// Renderer field holding the camera control (binary-verified: GetCameraControl 0x7C9FD0 returns it).
	constexpr size_t kRendererCameraControlOffset = 0x8C;
	// Multiplier applied to custom magnification for each wheel notch.
	constexpr float kMagnificationScalePerWheelNotch = 1.04f;
	// Lowest custom magnification retained within a native zoom stage.
	constexpr float kZoomRangeMagnificationMin = 0.5f;
	// Highest custom magnification retained within a native zoom stage.
	constexpr float kZoomRangeMagnificationMax = 2.0f;
	// Absolute lower safety bound for a native custom magnification value.
	constexpr float kMinimumMagnification = 0.001f;
	// Absolute upper safety bound for a native custom magnification value.
	constexpr float kMaximumMagnification = 10.0f;
	// Smallest valid horizontal camera-basis length for city-bounds calculations.
	constexpr float kCameraBasisEpsilon = 0.0001f;
	// Smallest native fit factor that can safely divide a magnification correction.
	constexpr float kMinimumOrthoFitFactor = 0.01f;
	// Smallest native scroll speed used to project a city-bounds probe.
	constexpr float kMinimumScrollProbeSpeed = 0.05f;
	// Minimum world-space distance used when SC4 supplies no usable zoom step.
	constexpr float kMinimumFallbackProbeDistance = 8.0f;
	// Fraction of orthographic scale used for a fallback city-bounds probe.
	constexpr float kFallbackProbeScale = 0.02f;
	// Number of native pitch and yaw table entries.
	constexpr size_t kZoomCount = 5;
	// Maximum float writes needed for one combined pitch and yaw table update.
	constexpr size_t kMaximumAnglePatchCount = kZoomCount * 3;
	// Maximum distinct virtual-memory pages occupied by the angle patches.
	constexpr size_t kMaximumPatchPageCount = 2;
	// Native pitch values restored for the five zoom stages.
	constexpr float kNativePitchByZoom[kZoomCount] = {
		0.52359879f,
		0.61086524f,
		0.69813170f,
		0.78539816f,
		0.78539816f,
	};
	// Doublings of on-screen size from zoom stage 0 to each stage, from the native orthographic
	// scales in default-camera.md (runtime-observed). Stages 2 and 3 are not exactly one doubling apart.
	const float kZoomStageLevels[kZoomCount] = {
		0.0f,
		1.0f,
		2.0f,
		std::log2(2.613126f / 0.286370f),
		std::log2(2.613126f / 0.143185f),
	};
	// One zoom level of the native Classic camera.
	struct ClassicZoomLevel
	{
		// Native zoom stage.
		int32_t zoom;
		// Custom magnification within the stage.
		float magnification;
	};
	// Number of native Classic zoom levels.
	constexpr size_t kClassicZoomLevelCount = 6;
	// Native Classic zoom levels, farthest first. The closest level stays on stage 4 at
	// magnification 2 (runtime-observed, default-camera.md).
	constexpr ClassicZoomLevel kClassicZoomLevels[kClassicZoomLevelCount] = {
		{ 0, 1.0f },
		{ 1, 1.0f },
		{ 2, 1.0f },
		{ 3, 1.0f },
		{ 4, 1.0f },
		{ 4, 2.0f },
	};
	// Length of a move between the Modern and Classic views, in seconds.
	constexpr float kClassicTransitionSeconds = 0.3f;
	// Overshoot constant of the move's easing. This value passes the target by about 8 percent of the distance.
	constexpr float kClassicTransitionOvershoot = 1.5f;
	// Deepest point of the drop played when Alt is let go, in doublings of on-screen size. About 6 percent.
	constexpr float kClassicDropZoomLevels = 0.08f;
	// Center of the native yaw bucket used when rebasing quarter turns.
	constexpr float kNativeYawAnchor = -SC4CameraController::kPi * 0.125f;
	// Radians in one native camera quarter turn.
	constexpr float kQuarterTurn = SC4CameraController::kPi * 0.5f;
	// Rebalance before SC4 enters the high-positive end of its native yaw
	// bucket, where building side meshes begin to disappear. The window is
	// deliberately wider than 90 degrees: this gives the quarter-turn handoff
	// hysteresis and maps either direction into the safe interior of the next
	// bucket instead of directly onto its opposite artifact boundary.
	constexpr float kMaxNativeYawOffset = 30.0f * SC4CameraController::kPi / 180.0f;
	constexpr float kMinNativeYawOffset = -70.0f * SC4CameraController::kPi / 180.0f;

	// First native pitch-table address used by the supported game executable.
	constexpr uintptr_t kPitchAddress1 = 0x00ABCFD8;
	// Second native pitch-table address used by the supported game executable.
	constexpr uintptr_t kPitchAddress2 = 0x00ABACCC;
	// Native yaw-table address used by the supported game executable. The constructor's yaw
	// operand (0x7CCB0A) and the unreferenced table at 0xABACB8 are deliberately left unpatched.
	constexpr uintptr_t kYawAddress1 = 0x00ABCFC4;

	// Signature of SC4's native routine that rebuilds camera transforms.
	using UpdateCameraPositionFn = bool(__thiscall*)(SC4CameraControlLayout*, uint32_t);
	// Signature of SC4's native routine that applies continuous zoom.
	using SetCustomMagnificationFn = bool(__thiscall*)(SC4CameraControlLayout*, float);
	// Signature of SC4's native routine that clamps the view target to the scroll polygon.
	using ClampViewTargetFn = void(__thiscall*)(SC4CameraControlLayout*);
	// Signature of SC4's native routine that re-inserts every model into a new render grid.
	using RebuildRenderGridFn = void(__thiscall*)(cISC43DRender*, int32_t, int32_t, int32_t, int32_t);

	// One float write deferred until all affected pages are writable.
	struct MemoryFloatPatch
	{
		// Native address that receives the replacement value.
		uintptr_t address;
		// Float value written once the containing page is writable.
		float value;
	};

	// Original protection and write status for a page touched by a patch batch.
	struct ProtectedMemoryPage
	{
		// Page-aligned address used for the temporary protection change.
		uintptr_t address = 0;
		// Original page protection restored after the batch completes.
		DWORD oldProtect = 0;
		// Whether VirtualProtect succeeded for this page.
		bool writable = false;
	};

	// Caches the operating system page size used to group protection changes.
	size_t GetSystemPageSize()
	{
		static const size_t pageSize = []() {
			SYSTEM_INFO systemInfo{};
			GetSystemInfo(&systemInfo);
			return static_cast<size_t>(systemInfo.dwPageSize);
		}();

		return pageSize;
	}

	// Records a failed native-table patch operation with its affected address.
	void LogMemoryPatchError(const char* message, uintptr_t address)
	{
		char hexAddress[32]{};
		sprintf_s(hexAddress, sizeof(hexAddress), "0x%08X", static_cast<uint32_t>(address));
		Logger::GetInstance().WriteLine(
			LogLevel::Error,
			std::string(message) + " at " + hexAddress);
	}

	// Applies a bounded set of native float writes with one protection change per page.
	class MemoryFloatPatchBatch
	{
	public:
		// Adds a changed float value to the fixed-capacity patch batch.
		void Add(uintptr_t address, float value)
		{
			if (std::memcmp(
				reinterpret_cast<const void*>(address),
				&value,
				sizeof(value)) == 0) {
				return;
			}
			if (patchCount >= patches.size()) {
				LogMemoryPatchError("Camera memory patch batch exceeded its fixed capacity", address);
				return;
			}

			patches[patchCount++] = { address, value };
		}

		// Makes each affected page writable, applies queued values, then restores protection.
		void Apply()
		{
			if (patchCount == 0) {
				return;
			}

			const size_t pageSize = GetSystemPageSize();
			std::array<ProtectedMemoryPage, kMaximumPatchPageCount> pages{};
			size_t pageCount = 0;

			for (size_t i = 0; i < patchCount; ++i) {
				const uintptr_t pageAddress = (patches[i].address / pageSize) * pageSize;
				if (FindPage(pages, pageCount, pageAddress)) {
					continue;
				}
				if (pageCount >= pages.size()) {
					LogMemoryPatchError("Camera memory patch batch exceeded its page capacity", pageAddress);
					continue;
				}

				ProtectedMemoryPage& page = pages[pageCount++];
				page.address = pageAddress;
				page.writable = VirtualProtect(
					reinterpret_cast<void*>(page.address),
					pageSize,
					PAGE_READWRITE,
					&page.oldProtect) != FALSE;
				if (!page.writable) {
					LogMemoryPatchError("VirtualProtect failed for camera memory page", page.address);
				}
			}

			for (size_t i = 0; i < patchCount; ++i) {
				const uintptr_t pageAddress = (patches[i].address / pageSize) * pageSize;
				ProtectedMemoryPage* page = FindPage(pages, pageCount, pageAddress);
				if (!page || !page->writable) {
					continue;
				}

				*reinterpret_cast<float*>(patches[i].address) = patches[i].value;
			}

			for (size_t i = pageCount; i > 0; --i) {
				const ProtectedMemoryPage& page = pages[i - 1];
				if (!page.writable) {
					continue;
				}

				DWORD ignored = 0;
				VirtualProtect(
					reinterpret_cast<void*>(page.address),
					pageSize,
					page.oldProtect,
					&ignored);
			}
		}

	private:
		// Finds a page already tracked by the current patch batch.
		static ProtectedMemoryPage* FindPage(
			std::array<ProtectedMemoryPage, kMaximumPatchPageCount>& pages,
			size_t pageCount,
			uintptr_t address)
		{
			for (size_t i = 0; i < pageCount; ++i) {
				if (pages[i].address == address) {
					return &pages[i];
				}
			}
			return nullptr;
		}

		// Pending float writes, bounded to the known pitch and yaw table sizes.
		std::array<MemoryFloatPatch, kMaximumAnglePatchCount> patches{};
		// Number of populated entries in patches.
		size_t patchCount = 0;
	};

	// Adds the two per-zoom native pitch-table writes for one pitch value.
	void AddPitchOverridePatches(MemoryFloatPatchBatch& patches, float pitch)
	{
		for (size_t i = 0; i < kZoomCount; ++i) {
			patches.Add(kPitchAddress1 + (i * sizeof(float)), pitch);
			patches.Add(kPitchAddress2 + (i * sizeof(float)), pitch);
		}
	}

	// Adds the per-zoom native yaw-table writes for one yaw value.
	void AddYawOverridePatches(MemoryFloatPatchBatch& patches, float yaw)
	{
		for (size_t i = 0; i < kZoomCount; ++i) {
			patches.Add(kYawAddress1 + (i * sizeof(float)), yaw);
		}
	}

	// Returns the native camera-position update entry point.
	UpdateCameraPositionFn GetUpdateCameraPosition()
	{
		return reinterpret_cast<UpdateCameraPositionFn>(kUpdateCameraPositionAddress);
	}

	// Returns the native custom-magnification entry point.
	SetCustomMagnificationFn GetSetCustomMagnification()
	{
		return reinterpret_cast<SetCustomMagnificationFn>(kSetCustomMagnificationAddress);
	}

	/// <summary>
	/// Returns the native view-target clamp entry point.
	/// </summary>
	/// <returns>The native routine that keeps the target inside the scroll polygon.</returns>
	ClampViewTargetFn GetClampViewTarget()
	{
		return reinterpret_cast<ClampViewTargetFn>(kClampViewTargetAddress);
	}

	/// <summary>
	/// Returns the native render-grid rebuild entry point.
	/// </summary>
	/// <returns>The native routine that re-inserts every model into a new render grid.</returns>
	RebuildRenderGridFn GetRebuildRenderGrid()
	{
		return reinterpret_cast<RebuildRenderGridFn>(kRebuildRenderGridAddress);
	}

	/// <summary>
	/// Returns the yaw-dependent factor SC4 multiplies into orthographic scale (binary-verified: 0x7F8300).
	/// </summary>
	/// <param name="yaw">Yaw in radians, as stored in the native table.</param>
	/// <returns>cos|yaw| + sin|yaw|.</returns>
	float GetOrthoFitFactor(float yaw)
	{
		const float absoluteYaw = std::abs(yaw);
		return std::cos(absoluteYaw) + std::sin(absoluteYaw);
	}

	/// <summary>
	/// Tells native subscribers the camera moved, as the native update does before its zoom/rotation messages.
	/// </summary>
	/// <param name="cameraControl">Camera control carried as the message payload.</param>
	void SendCameraUpdatedMessage(SC4CameraControlLayout& cameraControl)
	{
		cIGZMessageServer2Ptr messageServer;
		if (!messageServer) {
			return;
		}

		// The extra reference keeps a subscriber's AddRef/Release pair from deleting this stack object.
		cRZMessage2Standard message;
		message.AddRef();
		message.SetType(kSC4MessageCameraUpdated);
		message.SetVoid1(&cameraControl);
		messageServer->MessageSend(static_cast<cIGZMessage2Standard*>(&message));
	}

	// Returns the native camera-control structure owned by a renderer, if available.
	SC4CameraControlLayout* GetCameraControl(cISC43DRender* renderer)
	{
		return renderer
			? reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl())
			: nullptr;
	}

	template <typename Function>
	// Invokes a callback with the active 3D view and releases the acquired interface.
	bool WithView3D(Function&& function)
	{
		cISC4AppPtr pSC4App;
		if (!pSC4App) {
			return false;
		}

		cIGZWin* mainWindow = pSC4App->GetMainWindow();
		if (!mainWindow) {
			return false;
		}

		cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(SC4WindowID::MainApplication);
		if (!pParentWin) {
			return false;
		}

		cISC4View3DWin* pView3D = nullptr;
		if (!pParentWin->GetChildAs(
			SC4WindowID::View3D,
			kGZIID_cISC4View3DWin,
			reinterpret_cast<void**>(&pView3D))) {
			return false;
		}

		bool result = function(pView3D);

		pView3D->Release();
		return result;
	}

	template <typename Function>
	// Invokes a callback with the renderer associated with the active 3D view.
	bool WithRenderer(Function&& function)
	{
		return WithView3D([&](cISC4View3DWin* view3D) {
			cISC43DRender* renderer = view3D->GetRenderer();
			if (!renderer) {
				return false;
			}

			return function(renderer);
		});
	}

	// Reads one float from a native address used by the supported executable.
	float ReadMemoryFloat(uintptr_t address)
	{
		return *reinterpret_cast<float*>(address);
	}

	// Converts radians to degrees for diagnostics.
	float RadToDeg(float radians)
	{
		return radians * (180.0f / SC4CameraController::kPi);
	}

	// Returns the Euclidean length of a three-dimensional vector.
	float GetVectorLength(float x, float y, float z)
	{
		return std::sqrt((x * x) + (y * y) + (z * z));
	}

	// Returns the Euclidean length of a vector projected onto the XZ plane.
	float GetHorizontalLength(float x, float z)
	{
		return std::sqrt((x * x) + (z * z));
	}

	// Returns whether a city extent is finite and large enough for bounds checks.
	bool IsUsableCityExtent(float value)
	{
		return std::isfinite(value) && value > 1.0f;
	}

	// Returns whether a coordinate falls inside a usable city extent.
	bool IsInsideCityExtent(float value, float extent)
	{
		return IsUsableCityExtent(extent)
			&& std::isfinite(value)
			&& value >= 0.0f
			&& value <= extent;
	}

	// Clamps a finite coordinate to a usable city extent.
	float ClampToCityExtent(float value, float extent)
	{
		if (!IsUsableCityExtent(extent) || !std::isfinite(value)) {
			return value;
		}

		return std::clamp(value, 0.0f, extent);
	}

	// Clamps a requested target delta and returns whether bounds changed it.
	bool ClampPanDeltaToCityBounds(
		const SC4CameraControlLayout& cameraControl,
		float& deltaX,
		float& deltaZ)
	{
		if (!std::isfinite(cameraControl.viewTargetPosition.fX)
			|| !std::isfinite(cameraControl.viewTargetPosition.fZ)
			|| !std::isfinite(deltaX)
			|| !std::isfinite(deltaZ)) {
			return false;
		}

		const float requestedTargetX = cameraControl.viewTargetPosition.fX + deltaX;
		const float requestedTargetZ = cameraControl.viewTargetPosition.fZ + deltaZ;
		const float clampedTargetX = ClampToCityExtent(requestedTargetX, cameraControl.citySizeX);
		const float clampedTargetZ = ClampToCityExtent(requestedTargetZ, cameraControl.citySizeZ);

		deltaX = clampedTargetX - cameraControl.viewTargetPosition.fX;
		deltaZ = clampedTargetZ - cameraControl.viewTargetPosition.fZ;

		return requestedTargetX != clampedTargetX || requestedTargetZ != clampedTargetZ;
	}

	// Returns yaw offset from the nearest 90-degree rotation.
	float GetNearestQuarterTurnOffset(float yaw)
	{
		const float nearestQuarterTurn = std::round(yaw / kQuarterTurn) * kQuarterTurn;

		return yaw - nearestQuarterTurn;
	}

	// Returns yaw offset from the nearest native yaw-bucket anchor.
	float GetNativeYawAnchorOffset(float yaw)
	{
		const float nearestNativeBucket = kNativeYawAnchor
			+ (std::round((yaw - kNativeYawAnchor) / kQuarterTurn) * kQuarterTurn);

		return yaw - nearestNativeBucket;
	}

	// Wraps a native rotation index into the four valid quarter turns.
	int32_t WrapRotation(int32_t rotation)
	{
		rotation %= 4;
		if (rotation < 0) {
			rotation += 4;
		}

		return rotation;
	}

	// Rebases yaw into the configured native bucket and adjusts the rotation index.
	int32_t RebalanceYawAndRotation(float& yaw, int32_t& rotation)
	{
		int32_t quarterTurns = 0;

		while (yaw - kNativeYawAnchor > kMaxNativeYawOffset) {
			yaw -= kQuarterTurn;
			++quarterTurns;
		}

		while (yaw - kNativeYawAnchor < kMinNativeYawOffset) {
			yaw += kQuarterTurn;
			--quarterTurns;
		}

		rotation = WrapRotation(rotation + quarterTurns);
		return quarterTurns;
	}

	// Returns the nearest native-bucket offset and updates the rotation index.
	int32_t SnapYawToNearestNativeAngle(float yaw, int32_t& rotation)
	{
		const int32_t quarterTurns = static_cast<int32_t>(std::lround((yaw - kNativeYawAnchor) / kQuarterTurn));

		rotation = WrapRotation(rotation + quarterTurns);
		return quarterTurns;
	}

	// Translates a native vector along the city plane without changing its height.
	void TranslateXZ(cS3DVector3& vector, float deltaX, float deltaZ)
	{
		vector.fX += deltaX;
		vector.fZ += deltaZ;
	}

	// Formats a pointer value for diagnostic output.
	std::string FormatPointer(const void* value)
	{
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}

	// Maps expected city extents to a diagnostic tile-size label.
	const char* ClassifyCityTileSize(float sizeX, float sizeZ)
	{
		const float size = (std::max)(sizeX, sizeZ);
		const auto approximately = [size](float expected) {
			return std::fabs(size - expected) < 0.5f;
		};

		// Different camera implementations have reported these dimensions in
		// terrain cells (64/128/256) or world units (1024/2048/4096).
		if (approximately(64.0f) || approximately(1024.0f)) {
			return "Small";
		}
		if (approximately(128.0f) || approximately(2048.0f)) {
			return "Medium";
		}
		if (approximately(256.0f) || approximately(4096.0f)) {
			return "Large";
		}

		return "Unknown";
	}

	// Formats a native three-dimensional vector for diagnostic output.
	std::string FormatVector(const cS3DVector3& value)
	{
		return "X:" + std::to_string(value.fX)
			+ " Y:" + std::to_string(value.fY)
			+ " Z:" + std::to_string(value.fZ);
	}

	/// <summary>
	/// Returns whether two world positions are identical.
	/// </summary>
	/// <param name="first">First position.</param>
	/// <param name="second">Second position.</param>
	/// <returns>True when every component matches.</returns>
	bool IsSamePosition(const cS3DVector3& first, const cS3DVector3& second)
	{
		return first.fX == second.fX && first.fY == second.fY && first.fZ == second.fZ;
	}

	/// <summary>
	/// Returns a view's direction on the map. A quarter turn of rotation and a quarter
	/// turn of yaw look the same, which is what RebalanceYawAndRotation relies on.
	/// </summary>
	/// <param name="rotation">Native quarter-turn rotation.</param>
	/// <param name="yaw">Yaw in radians.</param>
	/// <returns>The direction in radians, not wrapped to any range.</returns>
	float GetViewAzimuth(int32_t rotation, float yaw)
	{
		return (static_cast<float>(rotation) * kQuarterTurn) + yaw;
	}

	/// <summary>
	/// Returns the on-screen zoom of a view on a scale where each unit doubles the size.
	/// Native orthoScale is fit(yaw) * K(zoom) / magnification.
	/// </summary>
	/// <param name="zoom">Native zoom stage.</param>
	/// <param name="magnification">Custom magnification within the stage.</param>
	/// <param name="yaw">Yaw in radians.</param>
	/// <returns>The on-screen zoom level.</returns>
	float GetOnScreenZoomLevel(int32_t zoom, float magnification, float yaw)
	{
		return kZoomStageLevels[std::clamp(zoom, 0, static_cast<int32_t>(kZoomCount - 1))]
			+ std::log2((std::max)(magnification, kMinimumMagnification))
			- std::log2((std::max)(GetOrthoFitFactor(yaw), kMinimumOrthoFitFactor));
	}

	/// <summary>
	/// Returns the Classic zoom level whose on-screen size is closest to a given zoom.
	/// </summary>
	/// <param name="onScreenZoomLevel">Zoom from GetOnScreenZoomLevel.</param>
	/// <returns>Index into kClassicZoomLevels.</returns>
	int32_t GetNearestClassicZoomLevel(float onScreenZoomLevel)
	{
		int32_t nearest = 0;
		float nearestDistance = FLT_MAX;
		for (size_t i = 0; i < kClassicZoomLevelCount; ++i) {
			const ClassicZoomLevel& level = kClassicZoomLevels[i];
			const float distance = std::abs(
				GetOnScreenZoomLevel(level.zoom, level.magnification, kNativeYawAnchor) - onScreenZoomLevel);
			if (distance < nearestDistance) {
				nearest = static_cast<int32_t>(i);
				nearestDistance = distance;
			}
		}
		return nearest;
	}

	/// <summary>
	/// Eases an Alt-hold move so it starts fast, passes its target slightly, and settles back.
	/// </summary>
	/// <param name="progress">Elapsed fraction of the move, from 0 to 1.</param>
	/// <returns>Fraction of the distance covered, which briefly exceeds 1.</returns>
	float EaseClassicTransition(float progress)
	{
		const float remaining = progress - 1.0f;
		return 1.0f
			+ ((kClassicTransitionOvershoot + 1.0f) * remaining * remaining * remaining)
			+ (kClassicTransitionOvershoot * remaining * remaining);
	}

	/// <summary>
	/// Returns how far the camera has dropped when Alt is let go. It sinks quickly and
	/// settles back to where it started.
	/// </summary>
	/// <param name="progress">Elapsed fraction of the move, from 0 to 1.</param>
	/// <returns>Zoom to add, on the scale of GetOnScreenZoomLevel.</returns>
	float GetClassicDropZoomLevels(float progress)
	{
		const float remaining = 1.0f - progress;
		return kClassicDropZoomLevels
			* std::sin(SC4CameraController::kPi * (1.0f - (remaining * remaining)));
	}

	// Formats a native four-float value for diagnostic output.
	std::string FormatFloat4(const SC4Float4& value)
	{
		return "X:" + std::to_string(value.fX)
			+ " Y:" + std::to_string(value.fY)
			+ " Z:" + std::to_string(value.fZ)
			+ " W:" + std::to_string(value.fW);
	}

	// Formats a contiguous float sequence for diagnostic output.
	std::string FormatFloatArray(const float* values, size_t count)
	{
		std::string result;

		for (size_t i = 0; i < count; i++) {
			if (!result.empty()) {
				result += ", ";
			}

			result += std::to_string(values[i]);
		}

		return result;
	}

	// Reads and formats a native float table for diagnostic output.
	std::string FormatMemoryFloatTable(uintptr_t address, size_t count)
	{
		std::string result;

		for (size_t i = 0; i < count; i++) {
			if (!result.empty()) {
				result += ", ";
			}

			result += std::to_string(ReadMemoryFloat(address + (i * sizeof(float))));
		}

		return result;
	}
}

// Initializes Modern Camera state before an active native camera is available.
SC4CameraController::SC4CameraController()
	: currentPitch(kDefaultPitch),
	  currentYaw(kDefaultYaw),
	  anglesInitialized(false),
	  classicAngleHoldActive(false),
	  classicAngleHoldSavedView{ 0, kDefaultYaw, kDefaultPitch, 0, 1.0f },
	  classicAngleHoldZoomLevel(0),
	  classicAngleHoldDragYaw(0.0f),
	  classicAngleHoldDragTurns(0),
	  classicAngleHoldWheelNotches(0.0f),
	  classicTransitionActive(false),
	  classicTransitionDrop(false),
	  classicTransitionElapsed(0.0f),
	  classicTransitionEased(0.0f),
	  classicTransitionFromAzimuth(0.0f),
	  classicTransitionYawTravel(0.0f),
	  classicTransitionFromPitch(kDefaultPitch),
	  classicTransitionFromZoomLevel(0.0f),
	  classicTransitionToZoomLevel(0.0f),
	  classicTransitionFromZoom(0),
	  classicTransitionTarget{ 0, kDefaultYaw, kDefaultPitch, 0, 1.0f },
	  rotationGestureActive(false),
	  rotationOrthoScale(0.0f),
	  rotationViewTarget{},
	  rotationBaseTarget{},
	  nativeCameraStateCaptured(false),
	  nativeCameraState{},
	  savePreviewNormalizationActive(false),
	  savedPitchTables{},
	  savedYawTable{},
	  savedPreviewPitch(kDefaultPitch),
	  savedPreviewYaw(kDefaultYaw),
	  savedPreviewViewTarget{},
	  savedPreviewBaseTarget{},
	  rotationGestureChanged(false),
	  viewSettlePending(false),
	  viewSettleKeepsTarget(false),
	  cameraUpdatePending(false),
	  gridRebuildPending(false),
	  redrawPending(false)
{
}

// Clears retained camera state and writes baseline values to native angle tables.
void SC4CameraController::Reset()
{
	RestoreNativeAngleTables();
	currentPitch = kDefaultPitch;
	currentYaw = kDefaultYaw;
	anglesInitialized = false;
	classicAngleHoldActive = false;
	classicAngleHoldSavedView = { 0, kDefaultYaw, kDefaultPitch, 0, 1.0f };
	classicAngleHoldZoomLevel = 0;
	classicAngleHoldDragYaw = 0.0f;
	classicAngleHoldDragTurns = 0;
	classicAngleHoldWheelNotches = 0.0f;
	classicTransitionActive = false;
	classicTransitionDrop = false;
	classicTransitionElapsed = 0.0f;
	classicTransitionEased = 0.0f;
	rotationGestureActive = false;
	rotationOrthoScale = 0.0f;
	rotationViewTarget = {};
	rotationBaseTarget = {};
	nativeCameraStateCaptured = false;
	nativeCameraState = {};
	rotationGestureChanged = false;
	viewSettlePending = false;
	viewSettleKeepsTarget = false;
	cameraUpdatePending = false;
	gridRebuildPending = false;
	redrawPending = false;
}

// Reports whether a native camera state can be restored.
bool SC4CameraController::HasNativeCameraState() const
{
	return nativeCameraStateCaptured;
}

// Exposes the native camera state captured before Modern Camera changed it.
const SC4NativeCameraState& SC4CameraController::GetNativeCameraState() const
{
	return nativeCameraState;
}

// Captures one native camera snapshot and preserves the first captured state.
bool SC4CameraController::EnsureNativeCameraStateCaptured(const SC4CameraControlLayout& cameraControl)
{
	if (nativeCameraStateCaptured) {
		return true;
	}

	nativeCameraState = {
		cameraControl.cameraPlaneOrigin,
		cameraControl.cameraPosition,
		cameraControl.viewTargetPosition,
		cameraControl.baseTargetForRotation,
		cameraControl.cameraOffset,
		cameraControl.customMagnification,
		cameraControl.prevZoom,
		cameraControl.prevRotation,
		cameraControl.zoom,
		cameraControl.rotation,
		cameraControl.postZoom,
		cameraControl.postRotation,
		cameraControl.yaw,
		cameraControl.pitch,
		cameraControl.cameraDistance,
		cameraControl.nearClip,
		cameraControl.farClip,
		cameraControl.orthoScale,
		cameraControl.invOrthoScale,
	};
	nativeCameraStateCaptured = true;

	Logger::GetInstance().WriteLine(
		LogLevel::Info,
		"Native Camera State Captured: Position[" + FormatVector(nativeCameraState.cameraPosition)
		+ "] ViewTarget[" + FormatVector(nativeCameraState.viewTargetPosition)
		+ "] Zoom:" + std::to_string(nativeCameraState.zoom)
		+ " Rotation:" + std::to_string(nativeCameraState.rotation)
		+ " Magnification:" + std::to_string(nativeCameraState.customMagnification)
		+ " Pitch:" + std::to_string(nativeCameraState.pitch)
		+ " Yaw:" + std::to_string(nativeCameraState.yaw));

	return true;
}

// Captures the first available native camera state for a later restoration.
bool SC4CameraController::CaptureNativeCameraState(const char* source)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Native Camera State Capture: camera control unavailable. Source:"
				+ std::string(source ? source : "unknown"));
			return false;
		}

		const bool alreadyCaptured = nativeCameraStateCaptured;
		const bool captured = EnsureNativeCameraStateCaptured(*cameraControl);
		if (captured && alreadyCaptured) {
			Logger::GetInstance().WriteLine(
				LogLevel::Verbose,
				"Native Camera State Capture: existing snapshot retained. Source:"
				+ std::string(source ? source : "unknown"));
		}
		return captured;
	});
}

// Copies the full restorable camera view without disturbing the baseline state
// retained for Classic-mode restoration.
bool SC4CameraController::CaptureCurrentCameraState(
	SC4NativeCameraState& state,
	const char* source) const
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Camera Bookmark Capture: camera control unavailable. Source:"
				+ std::string(source ? source : "unknown"));
			return false;
		}

		state = {
			cameraControl->cameraPlaneOrigin,
			cameraControl->cameraPosition,
			cameraControl->viewTargetPosition,
			cameraControl->baseTargetForRotation,
			cameraControl->cameraOffset,
			cameraControl->customMagnification,
			cameraControl->prevZoom,
			cameraControl->prevRotation,
			cameraControl->zoom,
			cameraControl->rotation,
			cameraControl->postZoom,
			cameraControl->postRotation,
			cameraControl->yaw,
			cameraControl->pitch,
			cameraControl->cameraDistance,
			cameraControl->nearClip,
			cameraControl->farClip,
			cameraControl->orthoScale,
			cameraControl->invOrthoScale,
		};
		return true;
	});
}

// Restores a view saved by the bookmark store. The persisted input is copied
// first, so bounds correction is never written back to the bookmark record.
bool SC4CameraController::RestoreCameraState(SC4NativeCameraState state, const char* source)
{
	FinishClassicAngleTransition();

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Camera Bookmark Restore: camera control unavailable. Source:"
				+ std::string(source ? source : "unknown"));
			return false;
		}

		const float clampedX = ClampToCityExtent(state.viewTargetPosition.fX, cameraControl->citySizeX);
		const float clampedZ = ClampToCityExtent(state.viewTargetPosition.fZ, cameraControl->citySizeZ);
		const float deltaX = clampedX - state.viewTargetPosition.fX;
		const float deltaZ = clampedZ - state.viewTargetPosition.fZ;
		if (deltaX != 0.0f || deltaZ != 0.0f) {
			TranslateXZ(state.cameraPlaneOrigin, deltaX, deltaZ);
			TranslateXZ(state.baseTargetForRotation, deltaX, deltaZ);
			TranslateXZ(state.cameraPosition, deltaX, deltaZ);
			TranslateXZ(state.viewTargetPosition, deltaX, deltaZ);
			// cameraOffset is part of the native absolute transform cache and
			// must move with the target to keep the restored view aligned.
			TranslateXZ(state.cameraOffset, deltaX, deltaZ);
		}

		if (cameraControl->zoom != state.zoom && !renderer->SetZoom(state.zoom)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmark Restore: renderer SetZoom failed.");
			return false;
		}
		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmark Restore: camera control unavailable after SetZoom.");
			return false;
		}

		if (cameraControl->rotation != state.rotation && !renderer->SetRotation(state.rotation)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmark Restore: renderer SetRotation failed.");
			return false;
		}
		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Bookmark Restore: camera control unavailable after SetRotation.");
			return false;
		}

		cameraControl->cameraPlaneOrigin = state.cameraPlaneOrigin;
		cameraControl->baseTargetForRotation = state.baseTargetForRotation;
		cameraControl->cameraPosition = state.cameraPosition;
		cameraControl->viewTargetPosition = state.viewTargetPosition;
		cameraControl->viewTargetVelocity = {};
		cameraControl->cameraOffset = state.cameraOffset;
		cameraControl->customMagnification = state.customMagnification;
		cameraControl->zoom = state.zoom;
		cameraControl->rotation = state.rotation;
		cameraControl->cameraDistance = state.cameraDistance;
		cameraControl->nearClip = state.nearClip;
		cameraControl->farClip = state.farClip;
		cameraControl->orthoScale = state.orthoScale;
		cameraControl->invOrthoScale = state.invOrthoScale;
		ApplyAngleOverrides(*cameraControl, state.pitch, state.yaw);

		currentPitch = SanitizePitch(state.pitch);
		currentYaw = SanitizeYaw(state.yaw);
		anglesInitialized = true;
		rotationGestureActive = false;
		rotationGestureChanged = false;
		rotationOrthoScale = 0.0f;
		rotationViewTarget = {};
		rotationBaseTarget = {};

		const bool refreshed = Refresh(*cameraControl);
		if (refreshed) {
			RequestViewSettle();
		}
		Logger::GetInstance().WriteLine(
			refreshed ? LogLevel::Info : LogLevel::Warning,
			std::string("Camera Bookmark Restore: ")
			+ (refreshed ? "restored" : "refresh failed")
			+ ". Source:" + (source ? source : "unknown")
			+ " BoundsCorrectionX:" + std::to_string(deltaX)
			+ " BoundsCorrectionZ:" + std::to_string(deltaZ));
		return refreshed;
	});
}

// Restores the captured native state and clears it after a successful refresh.
bool SC4CameraController::RestoreNativeCameraState(const char* source)
{
	if (!nativeCameraStateCaptured) {
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			"Native Camera State Restore: no captured state. Source:"
			+ std::string(source ? source : "unknown"));
		return false;
	}

	const SC4NativeCameraState state = nativeCameraState;
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Native Camera State Restore: camera control unavailable. Source:"
				+ std::string(source ? source : "unknown"));
			return false;
		}

		RestoreNativeAngleTables();

		if (cameraControl->zoom != state.zoom && !renderer->SetZoom(state.zoom)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Native Camera State Restore: renderer SetZoom failed.");
			return false;
		}
		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Native Camera State Restore: camera control unavailable after SetZoom.");
			return false;
		}

		if (cameraControl->rotation != state.rotation && !renderer->SetRotation(state.rotation)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Native Camera State Restore: renderer SetRotation failed.");
			return false;
		}
		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Native Camera State Restore: camera control unavailable after SetRotation.");
			return false;
		}

		cameraControl->cameraPlaneOrigin = state.cameraPlaneOrigin;
		cameraControl->baseTargetForRotation = state.baseTargetForRotation;
		cameraControl->cameraPosition = state.cameraPosition;
		cameraControl->viewTargetPosition = state.viewTargetPosition;
		cameraControl->viewTargetVelocity = {};
		cameraControl->cameraOffset = state.cameraOffset;
		cameraControl->customMagnification = state.customMagnification;
		cameraControl->prevZoom = state.prevZoom;
		cameraControl->prevRotation = state.prevRotation;
		cameraControl->zoom = state.zoom;
		cameraControl->rotation = state.rotation;
		cameraControl->postZoom = state.postZoom;
		cameraControl->postRotation = state.postRotation;
		cameraControl->yaw = state.yaw;
		cameraControl->pitch = state.pitch;
		cameraControl->cameraDistance = state.cameraDistance;
		cameraControl->nearClip = state.nearClip;
		cameraControl->farClip = state.farClip;
		cameraControl->orthoScale = state.orthoScale;
		cameraControl->invOrthoScale = state.invOrthoScale;

		currentPitch = state.pitch;
		currentYaw = state.yaw;
		anglesInitialized = false;
		rotationGestureActive = false;
		rotationGestureChanged = false;
		rotationOrthoScale = 0.0f;
		rotationViewTarget = {};
		rotationBaseTarget = {};

		const bool refreshed = Refresh(*cameraControl);
		if (refreshed) {
			nativeCameraStateCaptured = false;
			nativeCameraState = {};
			RequestViewSettle();
		}

		Logger::GetInstance().WriteLine(
			refreshed ? LogLevel::Info : LogLevel::Warning,
			std::string("Native Camera State Restore: ")
			+ (refreshed ? "restored" : "refresh failed")
			+ ". Source:" + (source ? source : "unknown")
			+ " Position[" + FormatVector(state.cameraPosition)
			+ "] ViewTarget[" + FormatVector(state.viewTargetPosition)
			+ "] Zoom:" + std::to_string(state.zoom)
			+ " Rotation:" + std::to_string(state.rotation)
			+ " Pitch:" + std::to_string(state.pitch)
			+ " Yaw:" + std::to_string(state.yaw)
			+ " SnapshotCleared:" + (refreshed ? "true" : "false"));
		return refreshed;
	});
}

// Temporarily applies baseline camera state before SC4 generates a save preview.
bool SC4CameraController::BeginSavePreviewNormalization()
{
	if (savePreviewNormalizationActive) {
		return false;
	}

	FinishClassicAngleTransition();

	const bool normalized = WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: camera control unavailable.");
			return false;
		}

		for (size_t i = 0; i < kZoomCount; ++i) {
			savedPitchTables[0][i] = ReadMemoryFloat(kPitchAddress1 + (i * sizeof(float)));
			savedPitchTables[1][i] = ReadMemoryFloat(kPitchAddress2 + (i * sizeof(float)));
			savedYawTable[i] = ReadMemoryFloat(kYawAddress1 + (i * sizeof(float)));
		}

		savedPreviewPitch = cameraControl->pitch;
		savedPreviewYaw = cameraControl->yaw;
		savedPreviewViewTarget = cameraControl->viewTargetPosition;
		savedPreviewBaseTarget = cameraControl->baseTargetForRotation;

		WriteNativeCameraAngleTables();

		const int32_t nativeZoom = std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1));
		cameraControl->pitch = kNativePitchByZoom[nativeZoom];
		cameraControl->yaw = kDefaultYaw;
		cameraControl->viewTargetVelocity = {};
		const bool refreshed = Refresh(*cameraControl);
		if (!refreshed) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: native camera refresh failed.");
			RestoreSavedCameraAngleTables();
			RestoreSavedPreviewCameraState(*cameraControl);
			return false;
		}

		savePreviewNormalizationActive = true;
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Save Preview: normalized camera from Pitch:" + std::to_string(savedPreviewPitch)
			+ " Yaw:" + std::to_string(savedPreviewYaw)
			+ " to native Pitch:" + std::to_string(kNativePitchByZoom[nativeZoom])
			+ " Yaw:" + std::to_string(kDefaultYaw)
			+ " Zoom:" + std::to_string(cameraControl->zoom)
			+ " Rotation:" + std::to_string(cameraControl->rotation));

		return true;
	});

	// SC4 renders the save preview later in this same pre-save broadcast, before any
	// frame runs, so the render grid must match the native angles right now.
	if (normalized) {
		RequestViewSettle();
		FlushPendingViewUpdates();
	}
	return normalized;
}

// Restores Modern Camera state retained by save-preview normalization.
void SC4CameraController::EndSavePreviewNormalization()
{
	if (!savePreviewNormalizationActive) {
		return;
	}

	RestoreSavedCameraAngleTables();

	const bool restored = WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			return false;
		}

		return RestoreSavedPreviewCameraState(*cameraControl);
	});

	savePreviewNormalizationActive = false;
	Logger::GetInstance().WriteLine(
		restored ? LogLevel::Info : LogLevel::Warning,
		restored ? "Save Preview: restored free-camera state." : "Save Preview: renderer unavailable after save; global tables restored.");
}

// Ends save-preview normalization without restoring the retained camera state.
void SC4CameraController::AbandonSavePreviewNormalization()
{
	if (!savePreviewNormalizationActive) {
		return;
	}

	savePreviewNormalizationActive = false;
	Logger::GetInstance().WriteLine(LogLevel::Info, "Save Preview: city is closing; retained native camera tables.");
}

// Reports whether a save-preview normalization is awaiting completion.
bool SC4CameraController::IsSavePreviewNormalizationActive() const
{
	return savePreviewNormalizationActive;
}

// Captures native target fields and scale before free-camera rotation begins.
bool SC4CameraController::BeginRotationGesture()
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			return false;
		}

		EnsureNativeCameraStateCaptured(*cameraControl);
		SyncAngles(*cameraControl);
		rotationViewTarget = cameraControl->viewTargetPosition;
		rotationBaseTarget = cameraControl->baseTargetForRotation;
		rotationOrthoScale = cameraControl->orthoScale;
		rotationGestureActive = true;
		rotationGestureChanged = false;

		Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
			return "Camera Rotation Anchor Frozen: ViewTarget[" + FormatVector(rotationViewTarget)
				+ "] BaseTarget[" + FormatVector(rotationBaseTarget)
				+ "] OrthoScale:" + std::to_string(rotationOrthoScale);
		});
		return true;
	});
}

// Releases the target fields retained for an active rotation gesture.
void SC4CameraController::EndRotationGesture()
{
	if (rotationGestureActive) {
		Logger::GetInstance().WriteLine(LogLevel::Verbose, "Camera Rotation Anchor Released.");
		if (rotationGestureChanged) {
			// Native subscribers were not told about individual rotation steps.
			RequestViewSettle();
		}
	}

	rotationGestureActive = false;
	rotationGestureChanged = false;
	rotationOrthoScale = 0.0f;
}

// Applies a bounded rotation delta and refreshes native camera state.
bool SC4CameraController::ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw)
{
	// During the hold a drag turns the view in Classic quarter turns, taking the nearest
	// turn to how far the mouse has moved, and vertical movement does nothing.
	if (classicAngleHoldActive) {
		classicAngleHoldDragYaw += yawDelta;
		const int32_t turns = static_cast<int32_t>(std::lround(classicAngleHoldDragYaw / kQuarterTurn))
			- classicAngleHoldDragTurns;
		if (turns == 0) {
			return true;
		}
		classicAngleHoldDragTurns += turns;
		return TurnClassicView(turns, "Alt+M3");
	}

	// Free rotation resumes once the camera is back on the saved Modern view.
	if (classicTransitionActive) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control from renderer.");
			return false;
		}

		SyncAngles(*cameraControl);

		// The previous step restored the anchor exactly, so any difference now is
		// held-key or right-drag panning. The pivot follows it instead of undoing it.
		if (rotationGestureActive) {
			rotationViewTarget = cameraControl->viewTargetPosition;
			rotationBaseTarget = cameraControl->baseTargetForRotation;
			cameraControl->viewTargetVelocity = {};
		}

		currentPitch = SanitizePitch(currentPitch + pitchDelta);
		currentYaw = SanitizeYaw(currentYaw + yawDelta);

		if (updateYaw) {
			int32_t targetRotation = cameraControl->rotation;
			const int32_t rebalanceTurns = RebalanceYawAndRotation(currentYaw, targetRotation);
			currentYaw = SanitizeYaw(currentYaw);

			if (rebalanceTurns != 0) {
				Logger::GetInstance().WriteLine(
					LogLevel::Info,
					"Camera Rotation Rebalance: OldRotation:" + std::to_string(cameraControl->rotation)
					+ " NewRotation:" + std::to_string(targetRotation)
					+ " RebalancedYaw:" + std::to_string(currentYaw)
					+ " QuarterTurns:" + std::to_string(rebalanceTurns));
			}

			if (targetRotation != cameraControl->rotation) {
				if (!renderer->SetRotation(targetRotation)) {
					Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Rotation Rebalance: SetRotation failed.");
					return false;
				}

				cameraControl = GetCameraControl(renderer);
				if (!cameraControl) {
					Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Rotation Rebalance: camera control unavailable after SetRotation.");
					return false;
				}

				// The base target is a city corner chosen by the rotation index.
				rotationBaseTarget = cameraControl->baseTargetForRotation;
				RestoreRotationAnchor(*cameraControl);
			}
		}

		// SC4 expands orthoScale as a square city rotates toward a diamond so the
		// corners remain inside the viewport. Counter that native bounds-fit with
		// magnification, preserving the visual zoom held by this gesture. Native
		// orthoScale is fit(yaw) * K(zoom) / magnification, so the magnification that
		// produces the held scale at the new yaw is known before the single refresh.
		if (rotationGestureActive && rotationOrthoScale > 0.0f
			&& cameraControl->orthoScale > 0.0f && cameraControl->customMagnification > 0.0f) {
			const size_t zoomIndex = static_cast<size_t>(
				std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1)));
			const float tableYaw = ReadMemoryFloat(kYawAddress1 + (zoomIndex * sizeof(float)));
			const float oldFit = GetOrthoFitFactor(tableYaw);
			const float newFit = GetOrthoFitFactor(SanitizeYaw(updateYaw ? currentYaw : tableYaw));

			if (oldFit > kMinimumOrthoFitFactor && newFit > kMinimumOrthoFitFactor) {
				cameraControl->customMagnification = std::clamp(
					cameraControl->customMagnification
					* (newFit / oldFit)
					* (cameraControl->orthoScale / rotationOrthoScale),
					kMinimumMagnification,
					kMaximumMagnification);
			}
		}

		if (updateYaw) {
			ApplyAngleOverrides(*cameraControl, currentPitch, currentYaw);
		}
		else {
			ApplyPitchOverride(currentPitch);
			cameraControl->pitch = currentPitch;
		}

		const bool result = Refresh(*cameraControl);
		RestoreRotationAnchor(*cameraControl);

		if (result) {
			rotationGestureChanged = true;
			cameraUpdatePending = true;
			gridRebuildPending = true;
		}
		return result;
	});
}

// Resolves the active renderer and clamps a native scroll request to city extents.
bool SC4CameraController::AdjustScrollForCityBounds(
	float directionAngle,
	float speed,
	float& adjustedDirectionAngle,
	bool& blocked,
	const char* source)
{
	adjustedDirectionAngle = directionAngle;
	blocked = false;

	return WithRenderer([&](cISC43DRender* renderer) {
		return AdjustScrollForCityBounds(
			renderer,
			directionAngle,
			speed,
			adjustedDirectionAngle,
			blocked,
			source);
	});
}

// Clamps a native scroll request using the renderer already owned by the caller.
bool SC4CameraController::AdjustScrollForCityBounds(
	cISC43DRender* renderer,
	float directionAngle,
	float speed,
	float& adjustedDirectionAngle,
	bool& blocked,
	const char* source)
{
	adjustedDirectionAngle = directionAngle;
	blocked = false;

	if (!renderer || !std::isfinite(directionAngle)) {
		return false;
	}

	SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
	if (!cameraControl) {
		Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control for scroll bounds preflight.");
		return false;
	}

	if (!std::isfinite(cameraControl->viewTargetPosition.fX)
		|| !std::isfinite(cameraControl->viewTargetPosition.fZ)) {
		return false;
	}

	if (!IsUsableCityExtent(cameraControl->citySizeX) || !IsUsableCityExtent(cameraControl->citySizeZ)) {
		blocked = true;
		Logger::GetInstance().WriteLine(
			LogLevel::Warning,
			std::string("Camera Scroll Bounds: unusable city extents; blocking scroll. Source:")
			+ (source ? source : "unknown")
			+ " CitySizeX:" + std::to_string(cameraControl->citySizeX)
			+ " CitySizeZ:" + std::to_string(cameraControl->citySizeZ));
		return true;
	}

	EnsureNativeCameraStateCaptured(*cameraControl);
	SyncAngles(*cameraControl);

	const float rightLength = GetHorizontalLength(
		cameraControl->cachedViewXformB.fX,
		cameraControl->cachedViewXformB.fZ);
	const float forwardLength = GetHorizontalLength(
		cameraControl->groundRayDirection.fX,
		cameraControl->groundRayDirection.fZ);
	if (rightLength <= kCameraBasisEpsilon || forwardLength <= kCameraBasisEpsilon) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Scroll Bounds: camera basis was invalid.");
		return false;
	}

	const float rightX = cameraControl->cachedViewXformB.fX / rightLength;
	const float rightZ = cameraControl->cachedViewXformB.fZ / rightLength;
	const float forwardX = cameraControl->groundRayDirection.fX / forwardLength;
	const float forwardZ = cameraControl->groundRayDirection.fZ / forwardLength;

	const float rightSteps = std::cos(directionAngle);
	const float forwardSteps = -std::sin(directionAngle);
	const int32_t zoomIndex = std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1));
	float probeDistance = cameraControl->zoomStepWorld[zoomIndex]
		* (std::max)(std::abs(speed), kMinimumScrollProbeSpeed);
	if (!std::isfinite(probeDistance) || probeDistance <= 0.0f) {
		probeDistance = (std::max)(
			kMinimumFallbackProbeDistance,
			cameraControl->orthoScale * kFallbackProbeScale);
	}

	const float requestedDeltaX = ((rightX * rightSteps) + (forwardX * forwardSteps)) * probeDistance;
	const float requestedDeltaZ = ((rightZ * rightSteps) + (forwardZ * forwardSteps)) * probeDistance;
	float deltaX = requestedDeltaX;
	float deltaZ = requestedDeltaZ;
	const bool clampedToCityBounds = ClampPanDeltaToCityBounds(*cameraControl, deltaX, deltaZ);
	if (!clampedToCityBounds) {
		return true;
	}

	const float requestedDotAdjusted = (requestedDeltaX * deltaX) + (requestedDeltaZ * deltaZ);
	const float adjustedDistance = GetHorizontalLength(deltaX, deltaZ);
	if (!std::isfinite(adjustedDistance)
		|| adjustedDistance <= kCameraBasisEpsilon
		|| !std::isfinite(requestedDotAdjusted)
		|| requestedDotAdjusted <= 0.0f) {
		blocked = true;
		Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
			return std::string("Camera Scroll Bounds: blocked outward scroll at city bounds. Source:")
				+ (source ? source : "unknown")
				+ " DirectionAngle:" + std::to_string(directionAngle)
				+ " Speed:" + std::to_string(speed)
				+ " RequestedDeltaX:" + std::to_string(requestedDeltaX)
				+ " RequestedDeltaZ:" + std::to_string(requestedDeltaZ)
				+ " AdjustedDeltaX:" + std::to_string(deltaX)
				+ " AdjustedDeltaZ:" + std::to_string(deltaZ);
		});
		return true;
	}

	const float adjustedRight = (deltaX * rightX) + (deltaZ * rightZ);
	const float adjustedForward = (deltaX * forwardX) + (deltaZ * forwardZ);
	const float adjustedInputLength = GetHorizontalLength(adjustedRight, adjustedForward);
	if (!std::isfinite(adjustedInputLength) || adjustedInputLength <= kCameraBasisEpsilon) {
		blocked = true;
		return true;
	}

	adjustedDirectionAngle = std::atan2(-adjustedForward, adjustedRight);
	Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
		return std::string("Camera Scroll Bounds: adjusted native scroll along city edge. Source:")
			+ (source ? source : "unknown")
			+ " DirectionAngle:" + std::to_string(directionAngle)
			+ " AdjustedDirectionAngle:" + std::to_string(adjustedDirectionAngle)
			+ " RequestedDeltaX:" + std::to_string(requestedDeltaX)
			+ " RequestedDeltaZ:" + std::to_string(requestedDeltaZ)
			+ " AdjustedDeltaX:" + std::to_string(deltaX)
			+ " AdjustedDeltaZ:" + std::to_string(deltaZ);
	});
	return true;
}

// Saves the Modern view and starts moving to the nearest Classic angle and zoom.
bool SC4CameraController::BeginClassicAngleHold(const char* source)
{
	if (classicAngleHoldActive) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold: camera control unavailable.");
			return false;
		}

		EnsureNativeCameraStateCaptured(*cameraControl);
		SyncAngles(*cameraControl);

		// A press during the return to Modern is not a new starting point. The view to
		// come back to is still the one saved by the first press.
		if (!classicTransitionActive) {
			classicAngleHoldSavedView = {
				cameraControl->rotation,
				currentYaw,
				currentPitch,
				cameraControl->zoom,
				cameraControl->customMagnification,
			};
		}
		const ClassicAngleView& saved = classicAngleHoldSavedView;

		int32_t targetRotation = saved.rotation;
		const int32_t snappedQuarterTurns = SnapYawToNearestNativeAngle(saved.yaw, targetRotation);
		classicAngleHoldZoomLevel = GetNearestClassicZoomLevel(
			GetOnScreenZoomLevel(saved.zoom, saved.magnification, saved.yaw));
		const ClassicZoomLevel& zoomLevel = kClassicZoomLevels[classicAngleHoldZoomLevel];
		const ClassicAngleView target = {
			targetRotation,
			kNativeYawAnchor,
			kNativePitchByZoom[zoomLevel.zoom],
			zoomLevel.zoom,
			zoomLevel.magnification,
		};

		classicAngleHoldDragYaw = 0.0f;
		classicAngleHoldDragTurns = 0;
		classicAngleHoldWheelNotches = 0.0f;
		classicAngleHoldActive = true;
		StartClassicAngleTransition(
			*cameraControl,
			target,
			SanitizeYaw(
				GetViewAzimuth(target.rotation, target.yaw)
				- GetViewAzimuth(cameraControl->rotation, cameraControl->yaw)));

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Classic Angle Hold: started. Source:") + (source ? source : "unknown")
			+ " SavedRotation:" + std::to_string(saved.rotation)
			+ " SavedYaw:" + std::to_string(saved.yaw)
			+ " SavedPitch:" + std::to_string(saved.pitch)
			+ " SavedZoom:" + std::to_string(saved.zoom)
			+ " SavedMagnification:" + std::to_string(saved.magnification)
			+ " ClassicRotation:" + std::to_string(target.rotation)
			+ " ClassicZoom:" + std::to_string(target.zoom)
			+ " ClassicMagnification:" + std::to_string(target.magnification)
			+ " QuarterTurns:" + std::to_string(snappedQuarterTurns));
		return true;
	});
}

// Reports whether temporary Classic-angle mode is currently active.
bool SC4CameraController::IsClassicAngleHoldActive() const
{
	return classicAngleHoldActive;
}

// Starts one Classic quarter turn in the direction plain Q or E turns the camera.
bool SC4CameraController::RotateClassicStep(bool left, const char* source)
{
	if (!classicAngleHoldActive) {
		return false;
	}

	return TurnClassicView(left ? 1 : -1, source);
}

// Starts Classic quarter turns from wherever the hold is heading.
bool SC4CameraController::TurnClassicView(int32_t turns, const char* source)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Turn: camera control unavailable.");
			return false;
		}

		// Added to what the current move has left, so quick presses add up instead of
		// taking the short way round.
		const float remainingTravel = GetClassicTransitionRemainingYaw();
		ClassicAngleView target = classicTransitionTarget;
		target.rotation = WrapRotation(
			(classicTransitionActive ? classicTransitionTarget.rotation : cameraControl->rotation) + turns);
		StartClassicAngleTransition(
			*cameraControl,
			target,
			remainingTravel + (static_cast<float>(turns) * kQuarterTurn));

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Classic Angle Turn: Source:") + (source ? source : "unknown")
			+ " Turns:" + std::to_string(turns)
			+ " ClassicRotation:" + std::to_string(target.rotation)
			+ " ViewTarget[" + FormatVector(cameraControl->viewTargetPosition) + "]");
		return true;
	});
}

// Moves to the next Classic zoom level for each whole wheel notch during the hold.
bool SC4CameraController::StepClassicZoom(float wheelNotches, bool& changed)
{
	changed = false;
	if (!classicAngleHoldActive) {
		return false;
	}

	// High-resolution wheels send fractions of a notch.
	classicAngleHoldWheelNotches += wheelNotches;
	const int32_t notches = static_cast<int32_t>(classicAngleHoldWheelNotches);
	classicAngleHoldWheelNotches -= static_cast<float>(notches);
	const int32_t zoomLevelIndex = std::clamp(
		classicAngleHoldZoomLevel + notches,
		0,
		static_cast<int32_t>(kClassicZoomLevelCount - 1));
	if (zoomLevelIndex == classicAngleHoldZoomLevel) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Zoom: camera control unavailable.");
			return false;
		}

		const ClassicZoomLevel& zoomLevel = kClassicZoomLevels[zoomLevelIndex];
		const ClassicAngleView target = {
			classicTransitionActive ? classicTransitionTarget.rotation : cameraControl->rotation,
			kNativeYawAnchor,
			kNativePitchByZoom[zoomLevel.zoom],
			zoomLevel.zoom,
			zoomLevel.magnification,
		};

		classicAngleHoldZoomLevel = zoomLevelIndex;
		StartClassicAngleTransition(*cameraControl, target, GetClassicTransitionRemainingYaw());
		changed = true;

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Classic Angle Zoom: ClassicZoom:" + std::to_string(target.zoom)
			+ " ClassicMagnification:" + std::to_string(target.magnification));
		return true;
	});
}

// Returns to the Modern view saved when the hold began.
bool SC4CameraController::EndClassicAngleHold(const char* source, bool animate)
{
	if (!classicAngleHoldActive) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold Restore: camera control unavailable.");
			return false;
		}

		const ClassicAngleView& saved = classicAngleHoldSavedView;
		classicAngleHoldActive = false;
		StartClassicAngleTransition(
			*cameraControl,
			saved,
			SanitizeYaw(
				GetViewAzimuth(saved.rotation, saved.yaw)
				- GetViewAzimuth(cameraControl->rotation, cameraControl->yaw)));

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Classic Angle Hold Restore: started. Source:") + (source ? source : "unknown")
			+ " HeldRotation:" + std::to_string(cameraControl->rotation)
			+ " HeldZoom:" + std::to_string(cameraControl->zoom));
		return animate ? true : CompleteClassicAngleTransition(renderer);
	});
}

// Ends the hold and stays on the view it reached, with a small drop of the camera.
bool SC4CameraController::DropClassicAngleHold(const char* source)
{
	if (!classicAngleHoldActive) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold Drop: camera control unavailable.");
			return false;
		}

		// A move still under way finishes, so a quick tap lands on a Classic view and not
		// on a point between two views.
		const ClassicAngleView target = classicTransitionActive
			? classicTransitionTarget
			: ClassicAngleView{
				cameraControl->rotation,
				cameraControl->yaw,
				cameraControl->pitch,
				cameraControl->zoom,
				cameraControl->customMagnification,
			};
		const float remainingTravel = GetClassicTransitionRemainingYaw();

		// A press during the drop has to come back to this view, not the old saved one.
		classicAngleHoldSavedView = target;
		classicAngleHoldActive = false;
		StartClassicAngleTransition(*cameraControl, target, remainingTravel);
		classicTransitionDrop = true;

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Classic Angle Hold Drop: started. Source:") + (source ? source : "unknown")
			+ " Rotation:" + std::to_string(target.rotation)
			+ " Zoom:" + std::to_string(target.zoom)
			+ " Magnification:" + std::to_string(target.magnification));
		return true;
	});
}

// Returns the turn the current move has left to make.
float SC4CameraController::GetClassicTransitionRemainingYaw() const
{
	return classicTransitionActive
		? classicTransitionYawTravel * (1.0f - classicTransitionEased)
		: 0.0f;
}

// Records where a move starts and where it ends. The frames do the moving.
void SC4CameraController::StartClassicAngleTransition(
	const SC4CameraControlLayout& cameraControl,
	const ClassicAngleView& target,
	float yawTravel)
{
	classicTransitionActive = true;
	classicTransitionDrop = false;
	classicTransitionElapsed = 0.0f;
	classicTransitionEased = 0.0f;
	classicTransitionFromAzimuth = GetViewAzimuth(cameraControl.rotation, cameraControl.yaw);
	classicTransitionYawTravel = yawTravel;
	classicTransitionFromPitch = cameraControl.pitch;
	classicTransitionFromZoomLevel = GetOnScreenZoomLevel(
		cameraControl.zoom,
		cameraControl.customMagnification,
		cameraControl.yaw);
	classicTransitionToZoomLevel = GetOnScreenZoomLevel(target.zoom, target.magnification, target.yaw);
	classicTransitionFromZoom = cameraControl.zoom;
	classicTransitionTarget = target;
}

// Applies this frame's point on the move between the Modern and Classic views.
void SC4CameraController::AdvanceClassicAngleTransition(float elapsedSeconds)
{
	if (!classicTransitionActive) {
		return;
	}

	classicTransitionElapsed += elapsedSeconds;
	if (classicTransitionElapsed >= kClassicTransitionSeconds) {
		FinishClassicAngleTransition();
		return;
	}

	const bool applied = WithRenderer([&](cISC43DRender* renderer) {
		const SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			return false;
		}

		const float progress = classicTransitionElapsed / kClassicTransitionSeconds;
		classicTransitionEased = EaseClassicTransition(progress);
		const float eased = classicTransitionEased;

		// Crossing the Modern yaw window hands over to the next rotation index, exactly as
		// M3 rotation does, which leaves the view unchanged.
		const float azimuth = classicTransitionFromAzimuth + (classicTransitionYawTravel * eased);
		float yaw = SanitizeYaw(azimuth - GetViewAzimuth(cameraControl->rotation, 0.0f));
		int32_t rotation = cameraControl->rotation;
		RebalanceYawAndRotation(yaw, rotation);
		yaw = SanitizeYaw(yaw);

		const float pitch = SanitizePitch(
			classicTransitionFromPitch + ((classicTransitionTarget.pitch - classicTransitionFromPitch) * eased));

		// The size changes evenly whichever stage draws it. Magnification makes up the
		// difference, including the fit factor of this frame's yaw.
		const int32_t zoom = eased < 0.5f ? classicTransitionFromZoom : classicTransitionTarget.zoom;
		const float zoomLevel = classicTransitionFromZoomLevel
			+ ((classicTransitionToZoomLevel - classicTransitionFromZoomLevel) * eased)
			+ (classicTransitionDrop ? GetClassicDropZoomLevels(progress) : 0.0f);
		const float magnification = std::clamp(
			std::pow(2.0f, zoomLevel - GetOnScreenZoomLevel(zoom, 1.0f, yaw)),
			kMinimumMagnification,
			kMaximumMagnification);

		return ApplyClassicAngleView(renderer, { rotation, yaw, pitch, zoom, magnification });
	});

	// Ending here leaves the camera in a valid view, and the next input starts from it.
	if (!applied) {
		classicTransitionActive = false;
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Transition: stopped because a frame could not be applied.");
	}
}

// Completes any move in progress at once.
void SC4CameraController::FinishClassicAngleTransition()
{
	if (!classicTransitionActive) {
		return;
	}

	WithRenderer([&](cISC43DRender* renderer) {
		return CompleteClassicAngleTransition(renderer);
	});
	classicTransitionActive = false;
}

// Ends the move on the exact target values, which the eased path only approaches.
bool SC4CameraController::CompleteClassicAngleTransition(cISC43DRender* renderer)
{
	classicTransitionActive = false;

	const bool applied = ApplyClassicAngleView(renderer, classicTransitionTarget);
	if (applied) {
		RequestViewSettle(true);
	}

	const SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
	if (cameraControl) {
		Logger::GetInstance().WriteLine(
			applied ? LogLevel::Info : LogLevel::Warning,
			std::string("Classic Angle Transition: ") + (applied ? "arrived" : "failed")
			+ ". ViewTarget[" + FormatVector(cameraControl->viewTargetPosition)
			+ "] Rotation:" + std::to_string(cameraControl->rotation)
			+ " Yaw:" + std::to_string(cameraControl->yaw)
			+ " Pitch:" + std::to_string(cameraControl->pitch)
			+ " Zoom:" + std::to_string(cameraControl->zoom)
			+ " Magnification:" + std::to_string(cameraControl->customMagnification)
			+ " OrthoScale:" + std::to_string(cameraControl->orthoScale));
	}
	return applied;
}

// Re-centers an invalid native target so camera input starts within city bounds.
bool SC4CameraController::EnsureTargetNearCityCenterIfOutOfBounds(const char* source)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera recenter: camera control unavailable.");
			return false;
		}

		if (!IsUsableCityExtent(cameraControl->citySizeX) || !IsUsableCityExtent(cameraControl->citySizeZ)) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Camera recenter: unusable city extents. Source:"
				+ std::string(source ? source : "unknown")
				+ " CitySizeX:" + std::to_string(cameraControl->citySizeX)
				+ " CitySizeZ:" + std::to_string(cameraControl->citySizeZ));
			return false;
		}

		const bool targetInside = IsInsideCityExtent(cameraControl->viewTargetPosition.fX, cameraControl->citySizeX)
			&& IsInsideCityExtent(cameraControl->viewTargetPosition.fZ, cameraControl->citySizeZ);
		if (targetInside) {
			Logger::GetInstance().WriteLine(
				LogLevel::Verbose,
				"Camera recenter: target already inside city bounds. Source:"
				+ std::string(source ? source : "unknown")
				+ " ViewTarget[" + FormatVector(cameraControl->viewTargetPosition) + "]");
			return true;
		}

		if (!std::isfinite(cameraControl->viewTargetPosition.fX)
			|| !std::isfinite(cameraControl->viewTargetPosition.fZ)) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Camera recenter: target was not finite. Source:"
				+ std::string(source ? source : "unknown")
				+ " ViewTarget[" + FormatVector(cameraControl->viewTargetPosition) + "]");
			return false;
		}

		EnsureNativeCameraStateCaptured(*cameraControl);
		SyncAngles(*cameraControl);

		const cS3DVector3 oldViewTarget = cameraControl->viewTargetPosition;
		const cS3DVector3 oldCameraPosition = cameraControl->cameraPosition;
		const float targetX = cameraControl->citySizeX * 0.5f;
		const float targetZ = cameraControl->citySizeZ * 0.5f;
		const float deltaX = targetX - cameraControl->viewTargetPosition.fX;
		const float deltaZ = targetZ - cameraControl->viewTargetPosition.fZ;

		TranslateXZ(cameraControl->cameraPlaneOrigin, deltaX, deltaZ);
		TranslateXZ(cameraControl->baseTargetForRotation, deltaX, deltaZ);
		TranslateXZ(cameraControl->cameraPosition, deltaX, deltaZ);
		TranslateXZ(cameraControl->viewTargetPosition, deltaX, deltaZ);
		cameraControl->viewTargetVelocity = {};
		TranslateXZ(cameraControl->cameraOffset, deltaX, deltaZ);
		cameraControl->pitch = currentPitch;
		cameraControl->yaw = currentYaw;

		const bool result = Refresh(*cameraControl);
		Logger::GetInstance().WriteLine(
			result ? LogLevel::Info : LogLevel::Warning,
			"Camera recenter: moved off-map target near city center. Source:"
			+ std::string(source ? source : "unknown")
			+ " OldViewTarget[" + FormatVector(oldViewTarget)
			+ "] NewViewTarget[" + FormatVector(cameraControl->viewTargetPosition)
			+ "] OldCameraPosition[" + FormatVector(oldCameraPosition)
			+ "] NewCameraPosition[" + FormatVector(cameraControl->cameraPosition)
			+ "] CitySizeX:" + std::to_string(cameraControl->citySizeX)
			+ " CitySizeZ:" + std::to_string(cameraControl->citySizeZ)
			+ " Result:" + std::to_string(result ? 1 : 0));
		if (result) {
			RequestViewSettle();
		}
		return result;
	});
}

// Converts a sensitivity-adjusted wheel delta into a fractional native zoom request.
bool SC4CameraController::ZoomByWheel(float wheelDelta, bool& changed)
{
	if (wheelDelta == 0.0f) {
		changed = false;
		return false;
	}

	return ZoomByNotches(wheelDelta / static_cast<float>(WHEEL_DELTA), changed);
}

// Applies continuous custom magnification while preserving Modern Camera angles.
bool SC4CameraController::ZoomByNotches(float wheelNotches, bool& changed)
{
	changed = false;

	// Free zoom resumes once the camera is back on the saved Modern view.
	if (classicTransitionActive) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control for zoom.");
			return false;
		}

		EnsureNativeCameraStateCaptured(*cameraControl);
		SyncAngles(*cameraControl);

		float targetMagnification = cameraControl->customMagnification
			* std::pow(kMagnificationScalePerWheelNotch, wheelNotches);
		int32_t targetZoom = cameraControl->zoom;

		const float oldMagnification = cameraControl->customMagnification;
		const int32_t oldZoom = cameraControl->zoom;
		const int32_t minZoom = static_cast<int32_t>(std::floor(cameraControl->minZoom));
		const int32_t maxZoom = static_cast<int32_t>(std::floor(cameraControl->maxZoomLevelWithoutCustom));
		const float minContinuousZoom = static_cast<float>(minZoom) + std::log2(kZoomRangeMagnificationMin);
		const float maxContinuousZoom = static_cast<float>(maxZoom) + std::log2(kZoomRangeMagnificationMax);

		targetMagnification = std::clamp(
			targetMagnification,
			kMinimumMagnification,
			kMaximumMagnification);

		while (targetMagnification > kZoomRangeMagnificationMax && targetZoom < maxZoom) {
			targetMagnification *= kZoomRangeMagnificationMin;
			++targetZoom;
		}

		while (targetMagnification < kZoomRangeMagnificationMin && targetZoom > minZoom) {
			targetMagnification *= kZoomRangeMagnificationMax;
			--targetZoom;
		}

		float targetContinuousZoom = static_cast<float>(targetZoom)
			+ std::log2((std::max)(targetMagnification, kMinimumMagnification));
		targetContinuousZoom = std::clamp(targetContinuousZoom, minContinuousZoom, maxContinuousZoom);

		targetZoom = std::clamp(static_cast<int32_t>(std::lround(targetContinuousZoom)), minZoom, maxZoom);
		targetMagnification = std::pow(2.0f, targetContinuousZoom - static_cast<float>(targetZoom));

		if (targetZoom == oldZoom && targetMagnification == oldMagnification) {
			return true;
		}

		if (targetZoom != oldZoom && !renderer->SetZoom(targetZoom)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Zoom: renderer SetZoom failed.");
			return false;
		}

		cameraControl = GetCameraControl(renderer);
		auto setCustomMagnification = GetSetCustomMagnification();

		if (!cameraControl || !setCustomMagnification) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Zoom: custom magnification thunk unavailable.");
			return false;
		}

		const bool magnificationResult = setCustomMagnification(cameraControl, targetMagnification);
		if (!magnificationResult) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Zoom: setting custom magnification failed.");
			return false;
		}

		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			return false;
		}

		// Zoom never owns orientation. Reapply the player's current pitch and yaw
		// after SC4 changes native zoom stages, which may otherwise reload its
		// per-stage angle tables.
		ApplyAngleOverrides(*cameraControl, currentPitch, currentYaw);
		const bool result = Refresh(*cameraControl);

		// Wheel zoom during a rotation gesture changes the visual zoom that gesture holds.
		if (result && rotationGestureActive) {
			rotationOrthoScale = cameraControl->orthoScale;
		}
		if (result) {
			// The render grid depends on orientation, not scale, so zoom never rebuilds it.
			cameraUpdatePending = true;
			redrawPending = true;
		}

		Logger::GetInstance().WriteLineLazy(LogLevel::Verbose, [&]() {
			return "Camera Zoom: Notches:" + std::to_string(wheelNotches)
				+ " OldZoom:" + std::to_string(oldZoom)
				+ " NewZoom:" + std::to_string(targetZoom)
				+ " OldMagnification:" + std::to_string(oldMagnification)
				+ " NewMagnification:" + std::to_string(targetMagnification)
				+ " Pitch:" + std::to_string(currentPitch)
				+ " Yaw:" + std::to_string(currentYaw)
				+ " Result:" + std::to_string(result ? 1 : 0);
		});

		changed = result;
		return result;
	});
}

// Requests a complete redraw through the active SC4 renderer.
bool SC4CameraController::ForceFullRedraw()
{
	return WithRenderer([](cISC43DRender* renderer) {
		return renderer->ForceFullRedraw();
	});
}

// Runs once per frame, before the draw system, so any number of camera changes
// costs at most one notification and one render-grid rebuild.
void SC4CameraController::FlushPendingViewUpdates()
{
	if (!viewSettlePending && !cameraUpdatePending && !gridRebuildPending && !redrawPending) {
		return;
	}

	const bool settle = viewSettlePending;
	const bool keepViewTarget = viewSettleKeepsTarget;
	const bool cameraUpdate = cameraUpdatePending;
	const bool rebuildGrid = gridRebuildPending;
	viewSettlePending = false;
	viewSettleKeepsTarget = false;
	cameraUpdatePending = false;
	gridRebuildPending = false;
	redrawPending = false;

	WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			return false;
		}

		if (settle) {
			// These fields are message payload that native setters fill and nothing clears.
			// While stale, each broadcast repeats the last discrete change for every
			// subscriber, including the model maker's pass over occupants and the
			// renderer's grid rebuild. Matching values report "no discrete change".
			cameraControl->prevZoom = cameraControl->zoom;
			cameraControl->prevRotation = cameraControl->rotation;
			cameraControl->postZoom = cameraControl->zoom;
			cameraControl->postRotation = cameraControl->rotation;

			const cS3DVector3 viewTarget = cameraControl->viewTargetPosition;
			auto updateCameraPosition = GetUpdateCameraPosition();
			updateCameraPosition(cameraControl, kCameraBroadcastUpdateMode);

			// The broadcast update ends with the native target clamp, which must not
			// move the focus during an Alt switch.
			if (keepViewTarget) {
				if (!IsSamePosition(cameraControl->viewTargetPosition, viewTarget)) {
					Logger::GetInstance().WriteLine(
						LogLevel::Info,
						"Camera View Update: kept the focus at [" + FormatVector(viewTarget)
						+ "]; the native clamp moved it to [" + FormatVector(cameraControl->viewTargetPosition) + "].");
				}
				cameraControl->viewTargetPosition = viewTarget;
				cameraControl->viewTargetVelocity = {};
				updateCameraPosition(cameraControl, kCameraSilentUpdateMode);
			}

			// Held-key panning may have moved the target since the last rotation step.
			if (rotationGestureActive) {
				rotationViewTarget = viewTarget;
				rotationBaseTarget = cameraControl->baseTargetForRotation;
			}
			RestoreRotationAnchor(*cameraControl);
		}
		else if (cameraUpdate) {
			SendCameraUpdatedMessage(*cameraControl);
		}

		const auto storedCameraControl = *reinterpret_cast<SC4CameraControlLayout* const*>(
			reinterpret_cast<const uint8_t*>(renderer) + kRendererCameraControlOffset);
		if (rebuildGrid && storedCameraControl == cameraControl) {
			// Grid positions use the current yaw and pitch, while the draw query uses
			// the current camera, so a stale grid hides models that are on screen.
			// The rebuild also requests the full redraw. It skips matching rotations,
			// so the previous rotation only has to differ from the current one.
			GetRebuildRenderGrid()(
				renderer,
				cameraControl->zoom,
				WrapRotation(cameraControl->rotation + 1),
				cameraControl->zoom,
				cameraControl->rotation);
		}
		else {
			if (rebuildGrid) {
				Logger::GetInstance().WriteLine(
					LogLevel::Error,
					"Camera View Update: renderer layout did not match; render grid was not rebuilt.");
			}
			renderer->ForceFullRedraw();
		}
		return true;
	});
}

// Returns the speed multiplier that cancels magnification for keyboard panning.
float SC4CameraController::GetPanSpeedZoomScale() const
{
	float scale = 1.0f;
	WithRenderer([&](cISC43DRender* renderer) {
		const SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl || cameraControl->customMagnification < kMinimumMagnification) {
			return false;
		}

		scale = 1.0f / cameraControl->customMagnification;
		return true;
	});
	return scale;
}

// Marks a discrete view change for one native notification and render-grid rebuild.
void SC4CameraController::RequestViewSettle(bool keepViewTarget)
{
	viewSettlePending = true;
	viewSettleKeepsTarget = viewSettleKeepsTarget || keepViewTarget;
	gridRebuildPending = true;
}

// Puts the camera in one view of an Alt-hold move, keeping the focus on the same point
// of the map.
bool SC4CameraController::ApplyClassicAngleView(cISC43DRender* renderer, const ClassicAngleView& view)
{
	SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
	if (!cameraControl) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle View: camera control unavailable.");
		return false;
	}

	const cS3DVector3 viewTarget = cameraControl->viewTargetPosition;
	const bool nativeStepNeeded = view.zoom != cameraControl->zoom || view.rotation != cameraControl->rotation;
	const bool orientationChanged = view.rotation != cameraControl->rotation
		|| view.pitch != cameraControl->pitch
		|| view.yaw != cameraControl->yaw;

	if (view.zoom != cameraControl->zoom && !renderer->SetZoom(view.zoom)) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle View: renderer SetZoom failed.");
		return false;
	}
	if (view.rotation != cameraControl->rotation && !renderer->SetRotation(view.rotation)) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle View: renderer SetRotation failed.");
		return false;
	}

	cameraControl = GetCameraControl(renderer);
	if (!cameraControl) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle View: camera control unavailable after the native step.");
		return false;
	}

	cameraControl->customMagnification = view.magnification;
	ApplyAngleOverrides(*cameraControl, view.pitch, view.yaw);

	// The native steps end with the target clamp, and this update deliberately has none.
	// Changing the view has no reason to move the focus. Without a native step any
	// target velocity is panning, which carries on through the move.
	if (nativeStepNeeded) {
		cameraControl->viewTargetPosition = viewTarget;
		cameraControl->viewTargetVelocity = {};
	}
	if (!RecomputeCamera(*cameraControl)) {
		Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle View: camera update failed.");
		return false;
	}

	currentPitch = SanitizePitch(view.pitch);
	currentYaw = SanitizeYaw(view.yaw);

	// An M3 or Q/E gesture still in progress continues from this view.
	if (rotationGestureActive) {
		rotationViewTarget = viewTarget;
		rotationBaseTarget = cameraControl->baseTargetForRotation;
		rotationOrthoScale = cameraControl->orthoScale;
	}
	RestoreRotationAnchor(*cameraControl);

	cameraUpdatePending = true;
	gridRebuildPending = gridRebuildPending || orientationChanged;
	return true;
}

// Logs available renderer and camera diagnostics, returning false when no renderer is available.
bool SC4CameraController::DumpCameraInfo(const char* reason) const
{
	Logger& log = Logger::GetInstance();

	log.WriteLine(LogLevel::Info, std::string("Camera Info Dump Begin: ") + (reason ? reason : "unspecified"));
	log.WriteLine(LogLevel::Info, "Controller Pitch:" + std::to_string(currentPitch)
		+ " Yaw:" + std::to_string(currentYaw));

	bool dumped = WithRenderer([&](cISC43DRender* renderer) {
		uint32_t viewportWidth = 0;
		uint32_t viewportHeight = 0;
		const bool hasViewportSize = renderer->GetViewportSize(viewportWidth, viewportHeight);

		log.WriteLine(LogLevel::Info, "Renderer: " + FormatPointer(renderer)
			+ " ViewportAvailable:" + std::to_string(hasViewportSize ? 1 : 0)
			+ " ViewportWidth:" + std::to_string(viewportWidth)
			+ " ViewportHeight:" + std::to_string(viewportHeight));

		cS3DCamera* camera = renderer->GetCamera();
		if (camera) {
			log.WriteLine(LogLevel::Info, "S3D Camera: " + FormatPointer(camera)
				+ " Position[" + FormatVector(camera->vPos) + "]");
		}
		else {
			log.WriteLine(LogLevel::Warning, "S3D Camera: null");
		}

		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			log.WriteLine(LogLevel::Warning, "Camera Control: null");
			return true;
		}

		log.WriteLine(LogLevel::Info, "Camera Control: " + FormatPointer(cameraControl)
			+ " VTable:" + FormatPointer(cameraControl->vtable)
			+ " RefCount:" + std::to_string(cameraControl->refCount)
			+ " CameraPtr:" + FormatPointer(cameraControl->camera));
		log.WriteLine(LogLevel::Info, "Camera Control Angles: Yaw:" + std::to_string(cameraControl->yaw)
			+ " Pitch:" + std::to_string(cameraControl->pitch)
			+ " YawDegrees:" + std::to_string(RadToDeg(cameraControl->yaw))
			+ " PitchDegrees:" + std::to_string(RadToDeg(cameraControl->pitch))
			+ " Distance:" + std::to_string(cameraControl->cameraDistance)
			+ " NearClip:" + std::to_string(cameraControl->nearClip)
			+ " FarClip:" + std::to_string(cameraControl->farClip));
		log.WriteLine(LogLevel::Info, "Camera Control Zoom/Rotation: PrevZoom:" + std::to_string(cameraControl->prevZoom)
			+ " Zoom:" + std::to_string(cameraControl->zoom)
			+ " PostZoom:" + std::to_string(cameraControl->postZoom)
			+ " MinZoom:" + std::to_string(cameraControl->minZoom)
			+ " MaxZoomNoCustom:" + std::to_string(cameraControl->maxZoomLevelWithoutCustom)
			+ " MaxZoom:" + std::to_string(cameraControl->maxZoom)
			+ " PrevRotation:" + std::to_string(cameraControl->prevRotation)
			+ " Rotation:" + std::to_string(cameraControl->rotation)
			+ " PostRotation:" + std::to_string(cameraControl->postRotation)
			+ " CustomMagnification:" + std::to_string(cameraControl->customMagnification));
		log.WriteLine(LogLevel::Info, "Camera Control Positions: CameraPosition[" + FormatVector(cameraControl->cameraPosition)
			+ "] ViewTarget[" + FormatVector(cameraControl->viewTargetPosition)
			+ "] ViewTargetVelocity[" + FormatVector(cameraControl->viewTargetVelocity)
			+ "] CameraOffset[" + FormatVector(cameraControl->cameraOffset) + "]");

		const float cameraToTargetX = cameraControl->cameraPosition.fX - cameraControl->viewTargetPosition.fX;
		const float cameraToTargetY = cameraControl->cameraPosition.fY - cameraControl->viewTargetPosition.fY;
		const float cameraToTargetZ = cameraControl->cameraPosition.fZ - cameraControl->viewTargetPosition.fZ;
		const float cameraToTargetHorizontal = GetHorizontalLength(cameraToTargetX, cameraToTargetZ);
		const float cameraToTarget3D = GetVectorLength(cameraToTargetX, cameraToTargetY, cameraToTargetZ);
		const float heightToHorizontalRatio = cameraToTargetHorizontal > 0.0f
			? cameraToTargetY / cameraToTargetHorizontal
			: 0.0f;
		const float nearClipToDistanceRatio = cameraControl->cameraDistance > 0.0f
			? cameraControl->nearClip / cameraControl->cameraDistance
			: 0.0f;
		const float farClipToDistanceRatio = cameraControl->cameraDistance > 0.0f
			? cameraControl->farClip / cameraControl->cameraDistance
			: 0.0f;
		const float nearClipToCameraTargetRatio = cameraToTarget3D > 0.0f
			? cameraControl->nearClip / cameraToTarget3D
			: 0.0f;

		log.WriteLine(LogLevel::Info, "Camera Diagnostics CameraToTarget: DeltaX:" + std::to_string(cameraToTargetX)
			+ " DeltaY:" + std::to_string(cameraToTargetY)
			+ " DeltaZ:" + std::to_string(cameraToTargetZ)
			+ " HeightOverTarget:" + std::to_string(cameraToTargetY)
			+ " HorizontalDistance:" + std::to_string(cameraToTargetHorizontal)
			+ " Distance3D:" + std::to_string(cameraToTarget3D)
			+ " HeightToHorizontalRatio:" + std::to_string(heightToHorizontalRatio));
		log.WriteLine(LogLevel::Info, "Camera Diagnostics ClipRatios: NearClipToCameraDistance:" + std::to_string(nearClipToDistanceRatio)
			+ " FarClipToCameraDistance:" + std::to_string(farClipToDistanceRatio)
			+ " NearClipToCameraTargetDistance:" + std::to_string(nearClipToCameraTargetRatio));
		log.WriteLine(LogLevel::Info, "Camera Diagnostics YawBuckets: NearestQuarterTurnOffsetRadians:" + std::to_string(GetNearestQuarterTurnOffset(cameraControl->yaw))
			+ " NearestQuarterTurnOffsetDegrees:" + std::to_string(RadToDeg(GetNearestQuarterTurnOffset(cameraControl->yaw)))
			+ " ActiveWindowOffsetRadians:" + std::to_string(cameraControl->yaw - kNativeYawAnchor)
			+ " ActiveWindowOffsetDegrees:" + std::to_string(RadToDeg(cameraControl->yaw - kNativeYawAnchor))
			+ " NativeAnchorOffsetRadians:" + std::to_string(GetNativeYawAnchorOffset(cameraControl->yaw))
			+ " NativeAnchorOffsetDegrees:" + std::to_string(RadToDeg(GetNativeYawAnchorOffset(cameraControl->yaw)))
			+ " NativeWindowMinDegrees:" + std::to_string(RadToDeg(kMinNativeYawOffset))
			+ " NativeWindowMaxDegrees:" + std::to_string(RadToDeg(kMaxNativeYawOffset)));

		if (camera) {
			const float s3dToControlX = camera->vPos.fX - cameraControl->cameraPosition.fX;
			const float s3dToControlY = camera->vPos.fY - cameraControl->cameraPosition.fY;
			const float s3dToControlZ = camera->vPos.fZ - cameraControl->cameraPosition.fZ;

			log.WriteLine(LogLevel::Info, "Camera Diagnostics S3DToControlCameraPosition: DeltaX:" + std::to_string(s3dToControlX)
				+ " DeltaY:" + std::to_string(s3dToControlY)
				+ " DeltaZ:" + std::to_string(s3dToControlZ)
				+ " Distance3D:" + std::to_string(GetVectorLength(s3dToControlX, s3dToControlY, s3dToControlZ)));
		}

		log.WriteLine(LogLevel::Info, "Camera Control Orientation: PlaneOrigin[" + FormatVector(cameraControl->cameraPlaneOrigin)
			+ "] BaseTargetForRotation[" + FormatVector(cameraControl->baseTargetForRotation)
			+ "] GroundRayDirection[" + FormatVector(cameraControl->groundRayDirection) + "]");
		log.WriteLine(LogLevel::Info, "Camera Control Cached Xforms: A[" + FormatVector(cameraControl->cachedViewXformA)
			+ "] B[" + FormatVector(cameraControl->cachedViewXformB) + "]");
		log.WriteLine(LogLevel::Info, "Camera Control Projection: GroundPlaneEq[" + FormatFloatArray(cameraControl->groundPlaneEq, 4)
			+ "] ProjectedCameraPlaneX:" + std::to_string(cameraControl->projectedCameraPlaneX)
			+ " ProjectedCameraPlaneY:" + std::to_string(cameraControl->projectedCameraPlaneY)
			+ " ProjectedScreenishX:" + std::to_string(cameraControl->projectedScreenishX)
			+ " ProjectedScreenishY:" + std::to_string(cameraControl->projectedScreenishY));
		log.WriteLine(LogLevel::Info, "Camera Control Prev Projection: PrevProjectedCameraPlaneX:" + std::to_string(cameraControl->prevProjectedCameraPlaneX)
			+ " PrevProjectedCameraPlaneY:" + std::to_string(cameraControl->prevProjectedCameraPlaneY)
			+ " PrevProjectedScreenishX:" + std::to_string(cameraControl->prevProjectedScreenishX)
			+ " PrevProjectedScreenishY:" + std::to_string(cameraControl->prevProjectedScreenishY));
		log.WriteLine(LogLevel::Info, "Camera Control Viewport: OffsetX:" + std::to_string(cameraControl->viewportOffsetX)
			+ " OffsetY:" + std::to_string(cameraControl->viewportOffsetY)
			+ " Width:" + std::to_string(cameraControl->viewportWidth)
			+ " Height:" + std::to_string(cameraControl->viewportHeight)
			+ " OrthoScale:" + std::to_string(cameraControl->orthoScale)
			+ " InvOrthoScale:" + std::to_string(cameraControl->invOrthoScale));
		log.WriteLine(
			LogLevel::Info,
			"Camera Diagnostics ProjectionHealth: OrthoScaleStatus:"
			+ std::string(cameraControl->orthoScale > 0.0f ? "Positive" : "NonPositive")
			+ " InvOrthoScaleStatus:"
			+ std::string(cameraControl->invOrthoScale > 0.0f ? "Positive" : "NonPositive"));
		log.WriteLine(LogLevel::Info, "Camera Control Bounds: BoundsA[" + FormatVector(cameraControl->boundsA)
			+ "] BoundsB[" + FormatVector(cameraControl->boundsB)
			+ "] CitySizeX:" + std::to_string(cameraControl->citySizeX)
			+ " CitySizeZ:" + std::to_string(cameraControl->citySizeZ)
			+ " CityTileSize:" + ClassifyCityTileSize(cameraControl->citySizeX, cameraControl->citySizeZ)
			+ " ScrollBoundsEnabled:" + std::to_string(cameraControl->scrollBoundsEnabled));
		log.WriteLine(LogLevel::Info, "Camera Control Scroll Bounds: Begin:" + FormatPointer(cameraControl->scrollBoundsBegin)
			+ " End:" + FormatPointer(cameraControl->scrollBoundsEnd)
			+ " CapacityEnd:" + FormatPointer(cameraControl->scrollBoundsCapacityEnd));
		log.WriteLine(LogLevel::Info, "Camera Control ZoomStepWorld: [" + FormatFloatArray(cameraControl->zoomStepWorld, 5) + "]");
		log.WriteLine(LogLevel::Info, "Camera Control Unknowns: unk04:" + std::to_string(cameraControl->unk04)
			+ " unk05:" + std::to_string(cameraControl->unk05)
			+ " unk28[" + FormatFloat4(cameraControl->unk28)
			+ "] unkCC:" + std::to_string(cameraControl->unkCC)
			+ " unkD0:" + std::to_string(cameraControl->unkD0)
			+ " unkD4:" + std::to_string(cameraControl->unkD4));
		log.WriteLine(LogLevel::Info, "Camera Diagnostics MemoryTables: PitchTable1[" + FormatMemoryFloatTable(kPitchAddress1, kZoomCount)
			+ "] PitchTable2[" + FormatMemoryFloatTable(kPitchAddress2, kZoomCount)
			+ "] YawTable1[" + FormatMemoryFloatTable(kYawAddress1, kZoomCount) + "]");

		return true;
	});

	if (!dumped) {
		log.WriteLine(LogLevel::Warning, "Camera Info Dump: renderer unavailable.");
	}

	log.WriteLine(LogLevel::Info, "Camera Info Dump End");
	return dumped;
}

// Displays the temporary cursor-text confirmation after a camera dump.
bool SC4CameraController::ShowCameraDumpConfirmation() const
{
	return WithView3D([](cISC4View3DWin* view3D) {
		static cRZBaseString title("SC4-ModernCamera");
		static cRZBaseString body("Camera info dumped to log");

		return view3D->SetCursorText(
			kCameraDumpCursorTextId,
			kCameraDumpCursorTextPriority,
			&title,
			&body,
			0);
	});
}

// Removes the cursor-text confirmation created after a camera dump.
bool SC4CameraController::ClearCameraDumpConfirmation() const
{
	return WithView3D([](cISC4View3DWin* view3D) {
		return view3D->ClearCursorText(kCameraDumpCursorTextId);
	});
}

// Uses static strings because the view can retain cursor-text pointers until
// the timer clears the acknowledgement.
bool SC4CameraController::ShowCameraBookmarkNotice(CameraBookmarkNotice notice, uint32_t slot) const
{
	static constexpr uint32_t kBookmarkSlotCount = 8;
	if (notice != CameraBookmarkNotice::StorageFailed && slot >= kBookmarkSlotCount)
	{
		return false;
	}

	return WithView3D([notice, slot](cISC4View3DWin* view3D) {
		static cRZBaseString title("SC4-Modern Camera");
		static cRZBaseString savedBodies[kBookmarkSlotCount] = {
			"User saved bookmark:  1",
			"User saved bookmark:  2",
			"User saved bookmark:  3",
			"User saved bookmark:  4",
			"User saved bookmark:  5",
			"User saved bookmark:  6",
			"User saved bookmark:  7",
			"User saved bookmark:  8",
		};
		static cRZBaseString recalledBodies[kBookmarkSlotCount] = {
			"Moved to bookmark:  1",
			"Moved to bookmark:  2",
			"Moved to bookmark:  3",
			"Moved to bookmark:  4",
			"Moved to bookmark:  5",
			"Moved to bookmark:  6",
			"Moved to bookmark:  7",
			"Moved to bookmark:  8",
		};
		static cRZBaseString emptyBodies[kBookmarkSlotCount] = {
			"No view saved in bookmark:  1",
			"No view saved in bookmark:  2",
			"No view saved in bookmark:  3",
			"No view saved in bookmark:  4",
			"No view saved in bookmark:  5",
			"No view saved in bookmark:  6",
			"No view saved in bookmark:  7",
			"No view saved in bookmark:  8",
		};
		static cRZBaseString saveFailedBodies[kBookmarkSlotCount] = {
			"Could not save bookmark:  1",
			"Could not save bookmark:  2",
			"Could not save bookmark:  3",
			"Could not save bookmark:  4",
			"Could not save bookmark:  5",
			"Could not save bookmark:  6",
			"Could not save bookmark:  7",
			"Could not save bookmark:  8",
		};
		static cRZBaseString recallFailedBodies[kBookmarkSlotCount] = {
			"Could not move to bookmark:  1",
			"Could not move to bookmark:  2",
			"Could not move to bookmark:  3",
			"Could not move to bookmark:  4",
			"Could not move to bookmark:  5",
			"Could not move to bookmark:  6",
			"Could not move to bookmark:  7",
			"Could not move to bookmark:  8",
		};
		static cRZBaseString storageFailedBody("Could not write the bookmark file");

		const cRZBaseString* body = &storageFailedBody;
		switch (notice) {
		case CameraBookmarkNotice::Saved:
			body = &savedBodies[slot];
			break;
		case CameraBookmarkNotice::Recalled:
			body = &recalledBodies[slot];
			break;
		case CameraBookmarkNotice::Empty:
			body = &emptyBodies[slot];
			break;
		case CameraBookmarkNotice::SaveFailed:
			body = &saveFailedBodies[slot];
			break;
		case CameraBookmarkNotice::RecallFailed:
			body = &recallFailedBodies[slot];
			break;
		case CameraBookmarkNotice::StorageFailed:
			break;
		}

		return view3D->SetCursorText(
			kCameraBookmarkCursorTextId,
			kCameraBookmarkCursorTextPriority,
			&title,
			body,
			0);
	});
}

// Removes the cursor-text acknowledgement created after a bookmark command.
bool SC4CameraController::ClearCameraBookmarkConfirmation() const
{
	return WithView3D([](cISC4View3DWin* view3D) {
		return view3D->ClearCursorText(kCameraBookmarkCursorTextId);
	});
}

// Normalizes angles, invokes SC4's silent camera update, and applies the same
// target clamp the broadcast update ends with. Callers request the notifications
// and redraw this leaves out through the pending-update flags.
bool SC4CameraController::Refresh(SC4CameraControlLayout& cameraControl)
{
	if (!RecomputeCamera(cameraControl)) {
		return false;
	}

	GetClampViewTarget()(&cameraControl);
	return true;
}

// Normalizes angles and invokes SC4's silent camera update.
bool SC4CameraController::RecomputeCamera(SC4CameraControlLayout& cameraControl)
{
	auto updateCameraPosition = GetUpdateCameraPosition();
	if (!updateCameraPosition) {
		return false;
	}

	cameraControl.pitch = SanitizePitch(cameraControl.pitch);
	cameraControl.yaw = SanitizeYaw(cameraControl.yaw);

	return updateCameraPosition(&cameraControl, kCameraSilentUpdateMode);
}

// Clamps pitch to the Modern Camera range.
float SC4CameraController::SanitizePitch(float pitch)
{
	return std::clamp(pitch, kMinPitch, kMaxPitch);
}

// Normalizes yaw to one full rotation centered on zero.
float SC4CameraController::SanitizeYaw(float yaw)
{
	float normalizedYaw = std::fmod(yaw, 2.0f * kPi);

	if (normalizedYaw <= -kPi) {
		normalizedYaw += 2.0f * kPi;
	}
	else if (normalizedYaw > kPi) {
		normalizedYaw -= 2.0f * kPi;
	}

	return normalizedYaw;
}

// Initializes tracked angles from native state once per camera reset.
void SC4CameraController::SyncAngles(const SC4CameraControlLayout& cameraControl)
{
	if (anglesInitialized) {
		return;
	}

	currentPitch = SanitizePitch(cameraControl.pitch);
	currentYaw = SanitizeYaw(cameraControl.yaw);
	anglesInitialized = true;
}

// Restores target fields captured before the active rotation gesture.
void SC4CameraController::RestoreRotationAnchor(SC4CameraControlLayout& cameraControl) const
{
	if (!rotationGestureActive) {
		return;
	}

	cameraControl.viewTargetPosition = rotationViewTarget;
	cameraControl.baseTargetForRotation = rotationBaseTarget;
	cameraControl.viewTargetVelocity = {};
}

// Writes the plugin's baseline pitch and yaw values to the native tables.
void SC4CameraController::WriteNativeCameraAngleTables()
{
	MemoryFloatPatchBatch patches;
	for (size_t i = 0; i < kZoomCount; i++) {
		patches.Add(kPitchAddress1 + (i * sizeof(float)), kNativePitchByZoom[i]);
		patches.Add(kPitchAddress2 + (i * sizeof(float)), kNativePitchByZoom[i]);
		patches.Add(kYawAddress1 + (i * sizeof(float)), kDefaultYaw);
	}
	patches.Apply();
}

// Restores baseline native tables and records the reset in the diagnostic log.
void SC4CameraController::RestoreNativeAngleTables()
{
	WriteNativeCameraAngleTables();
	Logger::GetInstance().WriteLine(LogLevel::Info, "Camera Reset: restored native global pitch/yaw tables.");
}

// Restores the pitch and yaw tables retained before save-preview normalization.
void SC4CameraController::RestoreSavedCameraAngleTables()
{
	MemoryFloatPatchBatch patches;
	for (size_t i = 0; i < kZoomCount; ++i) {
		patches.Add(kPitchAddress1 + (i * sizeof(float)), savedPitchTables[0][i]);
		patches.Add(kPitchAddress2 + (i * sizeof(float)), savedPitchTables[1][i]);
		patches.Add(kYawAddress1 + (i * sizeof(float)), savedYawTable[i]);
	}
	patches.Apply();
}

// Restores the camera-control fields retained before save-preview normalization.
bool SC4CameraController::RestoreSavedPreviewCameraState(SC4CameraControlLayout& cameraControl)
{
	cameraControl.pitch = savedPreviewPitch;
	cameraControl.yaw = savedPreviewYaw;
	cameraControl.viewTargetPosition = savedPreviewViewTarget;
	cameraControl.baseTargetForRotation = savedPreviewBaseTarget;
	cameraControl.viewTargetVelocity = {};

	const bool refreshed = Refresh(cameraControl);
	if (refreshed) {
		RequestViewSettle();
	}
	return refreshed;
}

// Writes pitch and yaw overrides together, then updates the supplied control fields.
void SC4CameraController::ApplyAngleOverrides(
	SC4CameraControlLayout& cameraControl,
	float pitch,
	float yaw)
{
	MemoryFloatPatchBatch patches;
	AddPitchOverridePatches(patches, SanitizePitch(pitch));
	AddYawOverridePatches(patches, SanitizeYaw(yaw));
	patches.Apply();
	cameraControl.pitch = pitch;
	cameraControl.yaw = yaw;
}

// Writes one bounded pitch value into every native pitch-table entry.
void SC4CameraController::ApplyPitchOverride(float pitch)
{
	pitch = SanitizePitch(pitch);

	MemoryFloatPatchBatch patches;
	AddPitchOverridePatches(patches, pitch);
	patches.Apply();
}
