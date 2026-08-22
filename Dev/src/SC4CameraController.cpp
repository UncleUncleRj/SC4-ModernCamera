#include "SC4CameraController.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "SC4GameConstants.h"
#include "cIGZWin.h"
#include "cISC43DRender.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cRZBaseString.h"
#include "cS3DCamera.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace
{
	// Native update mode used after direct camera-control changes.
	constexpr uint32_t kCameraUpdateMode = 2;
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
	// Minimum change worth correcting after native rotation adjusts orthographic scale.
	constexpr float kRotationFitCorrectionEpsilon = 0.0005f;
	// Smallest native scroll speed used to project a city-bounds probe.
	constexpr float kMinimumScrollProbeSpeed = 0.05f;
	// Minimum world-space distance used when SC4 supplies no usable zoom step.
	constexpr float kMinimumFallbackProbeDistance = 8.0f;
	// Fraction of orthographic scale used for a fallback city-bounds probe.
	constexpr float kFallbackProbeScale = 0.02f;
	// Number of native pitch and yaw table entries.
	constexpr size_t kZoomCount = 5;
	// Maximum float writes needed for one combined pitch and yaw table update.
	constexpr size_t kMaximumAnglePatchCount = (kZoomCount * 4) + 1;
	// Maximum distinct virtual-memory pages occupied by the angle patches.
	constexpr size_t kMaximumPatchPageCount = 3;
	// Native pitch values restored for the five zoom stages.
	constexpr float kNativePitchByZoom[kZoomCount] = {
		0.52359879f,
		0.61086524f,
		0.69813170f,
		0.78539816f,
		0.78539816f,
	};
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
	// Native yaw instruction address used by the supported game executable.
	constexpr uintptr_t kYawAddress0 = 0x007CCB0A;
	// First native yaw-table address used by the supported game executable.
	constexpr uintptr_t kYawAddress1 = 0x00ABCFC4;
	// Second native yaw-table address used by the supported game executable.
	constexpr uintptr_t kYawAddress2 = 0x00ABACB8;

	// Signature of SC4's native routine that rebuilds camera transforms.
	using UpdateCameraPositionFn = bool(__thiscall*)(SC4CameraControlLayout*, uint32_t);
	// Signature of SC4's native routine that applies continuous zoom.
	using SetCustomMagnificationFn = bool(__thiscall*)(SC4CameraControlLayout*, float);

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
		// Whether modified bytes require an instruction-cache flush.
		bool executable = false;
		// Whether this batch wrote at least one value into the page.
		bool modified = false;
	};

	// Returns whether a page protection allows instruction execution.
	bool IsExecutableProtection(DWORD protection)
	{
		switch (protection & 0xFF) {
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
		case PAGE_EXECUTE_READWRITE:
		case PAGE_EXECUTE_WRITECOPY:
			return true;
		default:
			return false;
		}
	}

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
					PAGE_EXECUTE_READWRITE,
					&page.oldProtect) != FALSE;
				if (!page.writable) {
					LogMemoryPatchError("VirtualProtect failed for camera memory page", page.address);
					continue;
				}
				page.executable = IsExecutableProtection(page.oldProtect);
			}

			for (size_t i = 0; i < patchCount; ++i) {
				const uintptr_t pageAddress = (patches[i].address / pageSize) * pageSize;
				ProtectedMemoryPage* page = FindPage(pages, pageCount, pageAddress);
				if (!page || !page->writable) {
					continue;
				}

				*reinterpret_cast<float*>(patches[i].address) = patches[i].value;
				page->modified = true;
			}

			for (size_t i = 0; i < pageCount; ++i) {
				const ProtectedMemoryPage& page = pages[i];
				if (page.writable && page.modified && page.executable) {
					FlushInstructionCache(
						GetCurrentProcess(),
						reinterpret_cast<void*>(page.address),
						pageSize);
				}
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

	// Adds the native yaw instruction and per-zoom yaw-table writes for one yaw value.
	void AddYawOverridePatches(MemoryFloatPatchBatch& patches, float yaw)
	{
		patches.Add(kYawAddress0, yaw);
		for (size_t i = 0; i < kZoomCount; ++i) {
			patches.Add(kYawAddress1 + (i * sizeof(float)), yaw);
			patches.Add(kYawAddress2 + (i * sizeof(float)), yaw);
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
	  classicAngleHoldPitch(kDefaultPitch),
	  classicAngleHoldYaw(kDefaultYaw),
	  classicAngleHoldRotation(0),
	  classicAngleHoldSnapYaw(kDefaultYaw),
	  classicAngleHoldSnapRotation(0),
	  rotationGestureActive(false),
	  rotationOrthoScale(0.0f),
	  rotationViewTarget{},
	  rotationBaseTarget{},
	  nativeCameraStateCaptured(false),
	  nativeCameraState{},
	  savePreviewNormalizationActive(false),
	  savedPitchTables{},
	  savedYawInstruction(kDefaultYaw),
	  savedYawTables{},
	  savedPreviewPitch(kDefaultPitch),
	  savedPreviewYaw(kDefaultYaw),
	  savedPreviewViewTarget{},
	  savedPreviewBaseTarget{}
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
	classicAngleHoldPitch = kDefaultPitch;
	classicAngleHoldYaw = kDefaultYaw;
	classicAngleHoldRotation = 0;
	classicAngleHoldSnapYaw = kDefaultYaw;
	classicAngleHoldSnapRotation = 0;
	rotationGestureActive = false;
	rotationOrthoScale = 0.0f;
	rotationViewTarget = {};
	rotationBaseTarget = {};
	nativeCameraStateCaptured = false;
	nativeCameraState = {};
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
		cameraControl->prevZoom = state.prevZoom;
		cameraControl->prevRotation = state.prevRotation;
		cameraControl->zoom = state.zoom;
		cameraControl->rotation = state.rotation;
		cameraControl->postZoom = state.postZoom;
		cameraControl->postRotation = state.postRotation;
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
		rotationOrthoScale = 0.0f;
		rotationViewTarget = {};
		rotationBaseTarget = {};

		const bool refreshed = Refresh(*cameraControl);
		if (refreshed) {
			renderer->ForceFullRedraw();
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
		rotationOrthoScale = 0.0f;
		rotationViewTarget = {};
		rotationBaseTarget = {};

		const bool refreshed = Refresh(*cameraControl);
		if (refreshed) {
			nativeCameraStateCaptured = false;
			nativeCameraState = {};
			renderer->ForceFullRedraw();
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

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: camera control unavailable.");
			return false;
		}

		for (size_t i = 0; i < kZoomCount; ++i) {
			savedPitchTables[0][i] = ReadMemoryFloat(kPitchAddress1 + (i * sizeof(float)));
			savedPitchTables[1][i] = ReadMemoryFloat(kPitchAddress2 + (i * sizeof(float)));
			savedYawTables[0][i] = ReadMemoryFloat(kYawAddress1 + (i * sizeof(float)));
			savedYawTables[1][i] = ReadMemoryFloat(kYawAddress2 + (i * sizeof(float)));
		}
		savedYawInstruction = ReadMemoryFloat(kYawAddress0);

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
		renderer->ForceFullRedraw();

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

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Camera Rotation Anchor Frozen: ViewTarget[" + FormatVector(rotationViewTarget)
			+ "] BaseTarget[" + FormatVector(rotationBaseTarget)
			+ "] OrthoScale:" + std::to_string(rotationOrthoScale));
		return true;
	});
}

// Releases the target fields retained for an active rotation gesture.
void SC4CameraController::EndRotationGesture()
{
	if (rotationGestureActive) {
		Logger::GetInstance().WriteLine(LogLevel::Info, "Camera Rotation Anchor Released.");
	}

	rotationGestureActive = false;
	rotationOrthoScale = 0.0f;
}

// Applies a bounded rotation delta and refreshes native camera state.
bool SC4CameraController::ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control from renderer.");
			return false;
		}

		SyncAngles(*cameraControl);
		RestoreRotationAnchor(*cameraControl);

		currentPitch = classicAngleHoldActive
			? currentPitch
			: SanitizePitch(currentPitch + pitchDelta);
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

				RestoreRotationAnchor(*cameraControl);
			}
		}

		if (updateYaw) {
			ApplyAngleOverrides(*cameraControl, currentPitch, currentYaw);
		}
		else {
			ApplyPitchOverride(currentPitch);
			cameraControl->pitch = currentPitch;
		}

		bool result = Refresh(*cameraControl);
		RestoreRotationAnchor(*cameraControl);

		// SC4 expands orthoScale as a square city rotates toward a diamond so the
		// corners remain inside the viewport. Counter that native bounds-fit with
		// magnification, preserving the visual zoom captured when M3 was pressed.
		if (result && rotationGestureActive && rotationOrthoScale > 0.0f
			&& cameraControl->orthoScale > 0.0f && cameraControl->customMagnification > 0.0f) {
			const float fitCorrection = cameraControl->orthoScale / rotationOrthoScale;

			if (std::abs(fitCorrection - 1.0f) > kRotationFitCorrectionEpsilon) {
				const float correctedMagnification = std::clamp(
					cameraControl->customMagnification * fitCorrection,
					kMinimumMagnification,
					kMaximumMagnification);
				auto setCustomMagnification = GetSetCustomMagnification();

				if (setCustomMagnification && setCustomMagnification(cameraControl, correctedMagnification)) {
					cameraControl = GetCameraControl(renderer);
					if (!cameraControl) {
						return false;
					}

					ApplyAngleOverrides(*cameraControl, currentPitch, currentYaw);
					RestoreRotationAnchor(*cameraControl);
					result = Refresh(*cameraControl);
					RestoreRotationAnchor(*cameraControl);
				}
			}
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

// Saves Modern Camera orientation and switches to the nearest Classic angle.
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

		classicAngleHoldPitch = currentPitch;
		classicAngleHoldYaw = currentYaw;
		classicAngleHoldRotation = cameraControl->rotation;

		const int32_t nativeZoom = std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1));
		int32_t targetRotation = cameraControl->rotation;
		const int32_t snappedQuarterTurns = SnapYawToNearestNativeAngle(currentYaw, targetRotation);
		const float targetPitch = kNativePitchByZoom[nativeZoom];
		const float targetYaw = kNativeYawAnchor;
		const cS3DVector3 viewTarget = cameraControl->viewTargetPosition;
		const cS3DVector3 baseTarget = cameraControl->baseTargetForRotation;

		if (targetRotation != cameraControl->rotation && !renderer->SetRotation(targetRotation)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold: renderer SetRotation failed.");
			return false;
		}

		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold: camera control unavailable after SetRotation.");
			return false;
		}

		ApplyAngleOverrides(*cameraControl, targetPitch, targetYaw);
		cameraControl->viewTargetPosition = viewTarget;
		cameraControl->baseTargetForRotation = baseTarget;
		cameraControl->viewTargetVelocity = {};

		const bool refreshed = Refresh(*cameraControl);
		RestoreRotationAnchor(*cameraControl);
		if (refreshed) {
			currentPitch = targetPitch;
			currentYaw = targetYaw;
			classicAngleHoldSnapYaw = targetYaw;
			classicAngleHoldSnapRotation = targetRotation;
			classicAngleHoldActive = true;
			renderer->ForceFullRedraw();
		}

		Logger::GetInstance().WriteLine(
			refreshed ? LogLevel::Info : LogLevel::Warning,
			std::string("Classic Angle Hold: ")
			+ (refreshed ? "started" : "refresh failed")
			+ ". Source:" + (source ? source : "unknown")
			+ " SavedPitch:" + std::to_string(classicAngleHoldPitch)
			+ " SavedYaw:" + std::to_string(classicAngleHoldYaw)
			+ " NativePitch:" + std::to_string(targetPitch)
			+ " NativeYaw:" + std::to_string(targetYaw)
			+ " SavedRotation:" + std::to_string(classicAngleHoldRotation)
			+ " NativeRotation:" + std::to_string(targetRotation)
			+ " QuarterTurns:" + std::to_string(snappedQuarterTurns));
		return refreshed;
	});
}

// Reports whether temporary Classic-angle mode is currently active.
bool SC4CameraController::IsClassicAngleHoldActive() const
{
	return classicAngleHoldActive;
}

// Performs one native Classic-camera quarter-turn while preserving the rotation anchor.
bool SC4CameraController::RotateClassicStep(bool left, const char* source)
{
	if (!classicAngleHoldActive) {
		return false;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Q/E: camera control unavailable.");
			return false;
		}

		const cS3DVector3 viewTarget = cameraControl->viewTargetPosition;
		const cS3DVector3 baseTarget = cameraControl->baseTargetForRotation;
		const bool rotated = left ? renderer->RotateLeft() : renderer->RotateRight();
		if (!rotated) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				std::string("Classic Angle Q/E: native ")
				+ (left ? "RotateLeft" : "RotateRight")
				+ " failed. Source:" + (source ? source : "unknown"));
			return false;
		}

		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(
				LogLevel::Warning,
				"Classic Angle Q/E: camera control unavailable after native rotation.");
			return false;
		}

		cameraControl->viewTargetPosition = viewTarget;
		cameraControl->baseTargetForRotation = baseTarget;
		cameraControl->viewTargetVelocity = {};
		SyncAngles(*cameraControl);
		RestoreRotationAnchor(*cameraControl);
		renderer->ForceFullRedraw();

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			std::string("Classic Angle Q/E: native ")
			+ (left ? "RotateLeft" : "RotateRight")
			+ " completed. Source:" + (source ? source : "unknown")
			+ " Rotation:" + std::to_string(cameraControl->rotation));
		return true;
	});
}

// Restores Modern Camera orientation saved by temporary Classic-angle mode.
bool SC4CameraController::EndClassicAngleHold(const char* source)
{
	if (!classicAngleHoldActive) {
		return true;
	}

	const float targetPitch = classicAngleHoldPitch;

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold Restore: camera control unavailable.");
			return false;
		}

		const cS3DVector3 viewTarget = cameraControl->viewTargetPosition;
		const cS3DVector3 baseTarget = cameraControl->baseTargetForRotation;
		const int32_t oldRotation = cameraControl->rotation;
		const float heldYaw = SanitizeYaw(cameraControl->yaw);
		const float yawDelta = SanitizeYaw(heldYaw - classicAngleHoldSnapYaw);
		const int32_t rotationDelta = cameraControl->rotation - classicAngleHoldSnapRotation;
		const float targetYaw = SanitizeYaw(classicAngleHoldYaw + yawDelta);
		const int32_t targetRotation = WrapRotation(classicAngleHoldRotation + rotationDelta);

		if (targetRotation != cameraControl->rotation && !renderer->SetRotation(targetRotation)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold Restore: renderer SetRotation failed.");
			return false;
		}

		cameraControl = GetCameraControl(renderer);
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Classic Angle Hold Restore: camera control unavailable after SetRotation.");
			return false;
		}

		ApplyAngleOverrides(*cameraControl, targetPitch, targetYaw);
		cameraControl->viewTargetPosition = viewTarget;
		cameraControl->baseTargetForRotation = baseTarget;
		cameraControl->viewTargetVelocity = {};

		const bool refreshed = Refresh(*cameraControl);
		RestoreRotationAnchor(*cameraControl);
		if (refreshed) {
			currentPitch = targetPitch;
			currentYaw = targetYaw;
			classicAngleHoldActive = false;
			renderer->ForceFullRedraw();
		}

		Logger::GetInstance().WriteLine(
			refreshed ? LogLevel::Info : LogLevel::Warning,
			std::string("Classic Angle Hold Restore: ")
			+ (refreshed ? "restored" : "refresh failed")
			+ ". Source:" + (source ? source : "unknown")
			+ " RestoredPitch:" + std::to_string(targetPitch)
			+ " RestoredYaw:" + std::to_string(targetYaw)
			+ " YawDelta:" + std::to_string(yawDelta)
			+ " OldRotation:" + std::to_string(oldRotation)
			+ " RestoredRotation:" + std::to_string(targetRotation)
			+ " RotationDelta:" + std::to_string(rotationDelta));
		return refreshed;
	});
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
			renderer->ForceFullRedraw();
		}
		return result;
	});
}

// Converts a Windows wheel delta into a fractional native zoom request.
bool SC4CameraController::ZoomByWheel(int32_t wheelDelta, bool& changed)
{
	if (wheelDelta == 0) {
		changed = false;
		return false;
	}

	return ZoomByNotches(static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA), changed);
}

// Applies continuous custom magnification while preserving Modern Camera angles.
bool SC4CameraController::ZoomByNotches(float wheelNotches, bool& changed)
{
	changed = false;

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

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Camera Zoom: Notches:" + std::to_string(wheelNotches)
			+ " OldZoom:" + std::to_string(oldZoom)
			+ " NewZoom:" + std::to_string(targetZoom)
			+ " OldMagnification:" + std::to_string(oldMagnification)
			+ " NewMagnification:" + std::to_string(targetMagnification)
			+ " Pitch:" + std::to_string(currentPitch)
			+ " Yaw:" + std::to_string(currentYaw)
			+ " Result:" + std::to_string(result ? 1 : 0));

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
			+ "] YawInstructionValue:" + std::to_string(ReadMemoryFloat(kYawAddress0))
			+ " YawTable1[" + FormatMemoryFloatTable(kYawAddress1, kZoomCount)
			+ "] YawTable2[" + FormatMemoryFloatTable(kYawAddress2, kZoomCount) + "]");

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
bool SC4CameraController::ShowCameraBookmarkConfirmation(bool saved, uint32_t slot) const
{
	static constexpr uint32_t kBookmarkSlotCount = 8;
	if (slot >= kBookmarkSlotCount)
	{
		return false;
	}

	return WithView3D([saved, slot](cISC4View3DWin* view3D) {
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

		return view3D->SetCursorText(
			kCameraBookmarkCursorTextId,
			kCameraBookmarkCursorTextPriority,
			&title,
			saved ? &savedBodies[slot] : &recalledBodies[slot],
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

// Normalizes angles and invokes SC4's native camera-update routine.
bool SC4CameraController::Refresh(SC4CameraControlLayout& cameraControl)
{
	auto updateCameraPosition = GetUpdateCameraPosition();
	if (!updateCameraPosition) {
		return false;
	}

	cameraControl.pitch = SanitizePitch(cameraControl.pitch);
	cameraControl.yaw = SanitizeYaw(cameraControl.yaw);

	return updateCameraPosition(&cameraControl, kCameraUpdateMode);
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
		patches.Add(kYawAddress2 + (i * sizeof(float)), kDefaultYaw);
	}
	patches.Add(kYawAddress0, kDefaultYaw);
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
		patches.Add(kYawAddress1 + (i * sizeof(float)), savedYawTables[0][i]);
		patches.Add(kYawAddress2 + (i * sizeof(float)), savedYawTables[1][i]);
	}
	patches.Add(kYawAddress0, savedYawInstruction);
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
	return Refresh(cameraControl);
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
