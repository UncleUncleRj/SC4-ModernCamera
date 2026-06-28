#include "SC4CameraController.h"

#include "GZServPtrs.h"
#include "Logger.h"
#include "cIGZWin.h"
#include "cISC43DRender.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "cRZBaseString.h"
#include "cS3DCamera.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

namespace
{
	constexpr uint32_t kGZWin_WinSC4App = 0x6104489a;
	constexpr uint32_t kGZWin_SC4View3DWin = 0x9a47b417;
	constexpr uint32_t kCameraUpdateMode = 2;
	constexpr uint32_t kCameraDumpCursorTextId = 0x3D0C4D01;
	constexpr uint32_t kCameraDumpCursorTextPriority = 1000;
	constexpr float kMagnificationScalePerWheelNotch = 1.04f;
	constexpr float kZoomRangeMagnificationMin = 0.5f;
	constexpr float kZoomRangeMagnificationMax = 2.0f;
	constexpr size_t kZoomCount = 5;
	constexpr float kNativePitchByZoom[kZoomCount] = {
		0.52359879f,
		0.61086524f,
		0.69813170f,
		0.78539816f,
		0.78539816f,
	};
	constexpr float kNativeYawAnchor = -SC4CameraController::kPi * 0.125f;
	// Rebalance before SC4 enters the high-positive end of its native yaw
	// bucket, where building side meshes begin to disappear. The window is
	// deliberately wider than 90 degrees: this gives the quarter-turn handoff
	// hysteresis and maps either direction into the safe interior of the next
	// bucket instead of directly onto its opposite artifact boundary.
	constexpr float kMaxNativeYawOffset = 30.0f * SC4CameraController::kPi / 180.0f;
	constexpr float kMinNativeYawOffset = -70.0f * SC4CameraController::kPi / 180.0f;

	constexpr uintptr_t kPitchAddress1 = 0x00ABCFD8;
	constexpr uintptr_t kPitchAddress2 = 0x00ABACCC;
	constexpr uintptr_t kYawAddress0 = 0x007CCB0A;
	constexpr uintptr_t kYawAddress1 = 0x00ABCFC4;
	constexpr uintptr_t kYawAddress2 = 0x00ABACB8;

	using UpdateCameraPositionFn = bool(__thiscall*)(SC4CameraControlLayout*, uint32_t);
	using SetCustomMagnificationFn = bool(__thiscall*)(SC4CameraControlLayout*, float);

	UpdateCameraPositionFn GetUpdateCameraPosition()
	{
		return reinterpret_cast<UpdateCameraPositionFn>(0x007CCF80);
	}

	SetCustomMagnificationFn GetSetCustomMagnification()
	{
		return reinterpret_cast<SetCustomMagnificationFn>(0x007CD6E0);
	}

	template <typename Function>
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

		cIGZWin* pParentWin = mainWindow->GetChildWindowFromID(kGZWin_WinSC4App);
		if (!pParentWin) {
			return false;
		}

		cISC4View3DWin* pView3D = nullptr;
		if (!pParentWin->GetChildAs(kGZWin_SC4View3DWin, kGZIID_cISC4View3DWin, reinterpret_cast<void**>(&pView3D))) {
			return false;
		}

		bool result = function(pView3D);

		pView3D->Release();
		return result;
	}

	template <typename Function>
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

	float ReadMemoryFloat(uintptr_t address)
	{
		return *reinterpret_cast<float*>(address);
	}

	float RadToDeg(float radians)
	{
		return radians * (180.0f / SC4CameraController::kPi);
	}

	float GetVectorLength(float x, float y, float z)
	{
		return std::sqrt((x * x) + (y * y) + (z * z));
	}

	float GetHorizontalLength(float x, float z)
	{
		return std::sqrt((x * x) + (z * z));
	}

	bool IsUsableCityExtent(float value)
	{
		return std::isfinite(value) && value > 1.0f;
	}

	float ClampToCityExtent(float value, float extent)
	{
		if (!IsUsableCityExtent(extent) || !std::isfinite(value)) {
			return value;
		}

		return std::clamp(value, 0.0f, extent);
	}

	bool ClampKeyboardPanDeltaToCityBounds(
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

	float GetNearestQuarterTurnOffset(float yaw)
	{
		constexpr float kQuarterTurn = SC4CameraController::kPi * 0.5f;
		const float nearestQuarterTurn = std::round(yaw / kQuarterTurn) * kQuarterTurn;

		return yaw - nearestQuarterTurn;
	}

	float GetNativeYawAnchorOffset(float yaw)
	{
		constexpr float kQuarterTurn = SC4CameraController::kPi * 0.5f;
		const float nearestNativeBucket = kNativeYawAnchor
			+ (std::round((yaw - kNativeYawAnchor) / kQuarterTurn) * kQuarterTurn);

		return yaw - nearestNativeBucket;
	}

	int32_t WrapRotation(int32_t rotation)
	{
		rotation %= 4;
		if (rotation < 0) {
			rotation += 4;
		}

		return rotation;
	}

	int32_t RebalanceYawAndRotation(float& yaw, int32_t& rotation)
	{
		constexpr float kQuarterTurn = SC4CameraController::kPi * 0.5f;

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

	std::string FormatPointer(const void* value)
	{
		std::ostringstream stream;
		stream << value;
		return stream.str();
	}

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

	std::string FormatVector(const cS3DVector3& value)
	{
		return "X:" + std::to_string(value.fX)
			+ " Y:" + std::to_string(value.fY)
			+ " Z:" + std::to_string(value.fZ);
	}

	std::string FormatFloat4(const SC4Float4& value)
	{
		return "X:" + std::to_string(value.fX)
			+ " Y:" + std::to_string(value.fY)
			+ " Z:" + std::to_string(value.fZ)
			+ " W:" + std::to_string(value.fW);
	}

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

SC4CameraController::SC4CameraController()
	: currentPitch(kDefaultPitch),
	  currentYaw(kDefaultYaw),
	  anglesInitialized(false),
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

void SC4CameraController::Reset()
{
	RestoreNativeAngleTables();
	currentPitch = kDefaultPitch;
	currentYaw = kDefaultYaw;
	anglesInitialized = false;
	rotationGestureActive = false;
	rotationOrthoScale = 0.0f;
	rotationViewTarget = {};
	rotationBaseTarget = {};
	nativeCameraStateCaptured = false;
	nativeCameraState = {};
}

bool SC4CameraController::HasNativeCameraState() const
{
	return nativeCameraStateCaptured;
}

const SC4NativeCameraState& SC4CameraController::GetNativeCameraState() const
{
	return nativeCameraState;
}

bool SC4CameraController::EnsureNativeCameraStateCaptured(const SC4CameraControlLayout& cameraControl)
{
	if (nativeCameraStateCaptured) {
		return true;
	}

	nativeCameraState = {
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

bool SC4CameraController::BeginSavePreviewNormalization()
{
	if (savePreviewNormalizationActive) {
		return false;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
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

		for (size_t i = 0; i < kZoomCount; ++i) {
			OverwriteMemoryFloat(kPitchAddress1 + (i * sizeof(float)), kNativePitchByZoom[i]);
			OverwriteMemoryFloat(kPitchAddress2 + (i * sizeof(float)), kNativePitchByZoom[i]);
			OverwriteMemoryFloat(kYawAddress1 + (i * sizeof(float)), kDefaultYaw);
			OverwriteMemoryFloat(kYawAddress2 + (i * sizeof(float)), kDefaultYaw);
		}
		OverwriteMemoryFloat(kYawAddress0, kDefaultYaw);

		const int32_t nativeZoom = std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1));
		cameraControl->pitch = kNativePitchByZoom[nativeZoom];
		cameraControl->yaw = kDefaultYaw;
		cameraControl->viewTargetVelocity = {};
		const bool refreshed = Refresh(*cameraControl);
		if (!refreshed) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Save Preview: native camera refresh failed.");
			for (size_t i = 0; i < kZoomCount; ++i) {
				OverwriteMemoryFloat(kPitchAddress1 + (i * sizeof(float)), savedPitchTables[0][i]);
				OverwriteMemoryFloat(kPitchAddress2 + (i * sizeof(float)), savedPitchTables[1][i]);
				OverwriteMemoryFloat(kYawAddress1 + (i * sizeof(float)), savedYawTables[0][i]);
				OverwriteMemoryFloat(kYawAddress2 + (i * sizeof(float)), savedYawTables[1][i]);
			}
			OverwriteMemoryFloat(kYawAddress0, savedYawInstruction);
			cameraControl->pitch = savedPreviewPitch;
			cameraControl->yaw = savedPreviewYaw;
			cameraControl->viewTargetPosition = savedPreviewViewTarget;
			cameraControl->baseTargetForRotation = savedPreviewBaseTarget;
			cameraControl->viewTargetVelocity = {};
			Refresh(*cameraControl);
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

void SC4CameraController::EndSavePreviewNormalization()
{
	if (!savePreviewNormalizationActive) {
		return;
	}

	for (size_t i = 0; i < kZoomCount; ++i) {
		OverwriteMemoryFloat(kPitchAddress1 + (i * sizeof(float)), savedPitchTables[0][i]);
		OverwriteMemoryFloat(kPitchAddress2 + (i * sizeof(float)), savedPitchTables[1][i]);
		OverwriteMemoryFloat(kYawAddress1 + (i * sizeof(float)), savedYawTables[0][i]);
		OverwriteMemoryFloat(kYawAddress2 + (i * sizeof(float)), savedYawTables[1][i]);
	}
	OverwriteMemoryFloat(kYawAddress0, savedYawInstruction);

	const bool restored = WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			return false;
		}

		cameraControl->pitch = savedPreviewPitch;
		cameraControl->yaw = savedPreviewYaw;
		cameraControl->viewTargetPosition = savedPreviewViewTarget;
		cameraControl->baseTargetForRotation = savedPreviewBaseTarget;
		cameraControl->viewTargetVelocity = {};
		return Refresh(*cameraControl);
	});

	savePreviewNormalizationActive = false;
	Logger::GetInstance().WriteLine(
		restored ? LogLevel::Info : LogLevel::Warning,
		restored ? "Save Preview: restored free-camera state." : "Save Preview: renderer unavailable after save; global tables restored.");
}

void SC4CameraController::AbandonSavePreviewNormalization()
{
	if (!savePreviewNormalizationActive) {
		return;
	}

	savePreviewNormalizationActive = false;
	Logger::GetInstance().WriteLine(LogLevel::Info, "Save Preview: city is closing; retained native camera tables.");
}

bool SC4CameraController::IsSavePreviewNormalizationActive() const
{
	return savePreviewNormalizationActive;
}

bool SC4CameraController::BeginRotationGesture()
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
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

void SC4CameraController::EndRotationGesture()
{
	if (rotationGestureActive) {
		Logger::GetInstance().WriteLine(LogLevel::Info, "Camera Rotation Anchor Released.");
	}

	rotationGestureActive = false;
	rotationOrthoScale = 0.0f;
}

bool SC4CameraController::ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control from renderer.");
			return false;
		}

		SyncAngles(*cameraControl);
		RestoreRotationAnchor(*cameraControl);

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

				cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
				if (!cameraControl) {
					Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Rotation Rebalance: camera control unavailable after SetRotation.");
					return false;
				}

				RestoreRotationAnchor(*cameraControl);
			}
		}

		ApplyPitchOverride(currentPitch);

		if (updateYaw) {
			ApplyYawOverride(currentYaw);
			cameraControl->yaw = currentYaw;
		}

		cameraControl->pitch = currentPitch;

		bool result = Refresh(*cameraControl);
		RestoreRotationAnchor(*cameraControl);

		// SC4 expands orthoScale as a square city rotates toward a diamond so the
		// corners remain inside the viewport. Counter that native bounds-fit with
		// magnification, preserving the visual zoom captured when M3 was pressed.
		if (result && rotationGestureActive && rotationOrthoScale > 0.0f
			&& cameraControl->orthoScale > 0.0f && cameraControl->customMagnification > 0.0f) {
			const float fitCorrection = cameraControl->orthoScale / rotationOrthoScale;

			if (std::abs(fitCorrection - 1.0f) > 0.0005f) {
				const float correctedMagnification = std::clamp(
					cameraControl->customMagnification * fitCorrection,
					0.001f,
					10.0f);
				auto setCustomMagnification = GetSetCustomMagnification();

				if (setCustomMagnification && setCustomMagnification(cameraControl, correctedMagnification)) {
					cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
					if (!cameraControl) {
						return false;
					}

					ApplyPitchOverride(currentPitch);
					ApplyYawOverride(currentYaw);
					cameraControl->pitch = currentPitch;
					cameraControl->yaw = currentYaw;
					RestoreRotationAnchor(*cameraControl);
					result = Refresh(*cameraControl);
					RestoreRotationAnchor(*cameraControl);
				}
			}
		}

		return result;
	});
}

bool SC4CameraController::PanByKeyboard(float rightSteps, float forwardSteps)
{
	if (rightSteps == 0.0f && forwardSteps == 0.0f) {
		return true;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control for keyboard pan.");
			return false;
		}

		EnsureNativeCameraStateCaptured(*cameraControl);
		SyncAngles(*cameraControl);

		const float rightLength = GetHorizontalLength(
			cameraControl->cachedViewXformB.fX,
			cameraControl->cachedViewXformB.fZ);
		const float forwardLength = GetHorizontalLength(
			cameraControl->groundRayDirection.fX,
			cameraControl->groundRayDirection.fZ);
		if (rightLength <= 0.0001f || forwardLength <= 0.0001f) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Keyboard Pan: camera basis was invalid.");
			return false;
		}

		const float rightX = cameraControl->cachedViewXformB.fX / rightLength;
		const float rightZ = cameraControl->cachedViewXformB.fZ / rightLength;
		const float forwardX = cameraControl->groundRayDirection.fX / forwardLength;
		const float forwardZ = cameraControl->groundRayDirection.fZ / forwardLength;

		const int32_t zoomIndex = std::clamp(cameraControl->zoom, 0, static_cast<int32_t>(kZoomCount - 1));
		float stepSize = cameraControl->zoomStepWorld[zoomIndex];
		if (!std::isfinite(stepSize) || stepSize <= 0.0f) {
			stepSize = (std::max)(8.0f, cameraControl->orthoScale * 0.02f);
		}

		const float requestedDeltaX = ((rightX * rightSteps) + (forwardX * forwardSteps)) * stepSize;
		const float requestedDeltaZ = ((rightZ * rightSteps) + (forwardZ * forwardSteps)) * stepSize;
		float deltaX = requestedDeltaX;
		float deltaZ = requestedDeltaZ;
		const bool clampedToCityBounds = ClampKeyboardPanDeltaToCityBounds(*cameraControl, deltaX, deltaZ);

		if (!std::isfinite(deltaX) || !std::isfinite(deltaZ)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Keyboard Pan: computed delta was invalid.");
			return false;
		}

		if (deltaX == 0.0f && deltaZ == 0.0f) {
			Logger::GetInstance().WriteLine(
				LogLevel::Info,
				"Camera Keyboard Pan: clamped at city bounds. RightSteps:" + std::to_string(rightSteps)
				+ " ForwardSteps:" + std::to_string(forwardSteps)
				+ " StepSize:" + std::to_string(stepSize)
				+ " RequestedDeltaX:" + std::to_string(requestedDeltaX)
				+ " RequestedDeltaZ:" + std::to_string(requestedDeltaZ));
			return true;
		}

		cameraControl->cameraPlaneOrigin.fX += deltaX;
		cameraControl->cameraPlaneOrigin.fZ += deltaZ;
		cameraControl->baseTargetForRotation.fX += deltaX;
		cameraControl->baseTargetForRotation.fZ += deltaZ;
		cameraControl->cameraPosition.fX += deltaX;
		cameraControl->cameraPosition.fZ += deltaZ;
		cameraControl->viewTargetPosition.fX += deltaX;
		cameraControl->viewTargetPosition.fZ += deltaZ;
		cameraControl->viewTargetVelocity.fX = 0.0f;
		cameraControl->viewTargetVelocity.fY = 0.0f;
		cameraControl->viewTargetVelocity.fZ = 0.0f;
		cameraControl->cameraOffset.fX += deltaX;
		cameraControl->cameraOffset.fZ += deltaZ;

		ApplyPitchOverride(currentPitch);
		ApplyYawOverride(currentYaw);
		cameraControl->pitch = currentPitch;
		cameraControl->yaw = currentYaw;

		const bool result = Refresh(*cameraControl);
		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Camera Keyboard Pan: RightSteps:" + std::to_string(rightSteps)
			+ " ForwardSteps:" + std::to_string(forwardSteps)
			+ " StepSize:" + std::to_string(stepSize)
			+ " RequestedDeltaX:" + std::to_string(requestedDeltaX)
			+ " RequestedDeltaZ:" + std::to_string(requestedDeltaZ)
			+ " DeltaX:" + std::to_string(deltaX)
			+ " DeltaZ:" + std::to_string(deltaZ)
			+ " Clamped:" + std::to_string(clampedToCityBounds ? 1 : 0)
			+ " Result:" + std::to_string(result ? 1 : 0));
		return result;
	});
}

bool SC4CameraController::ZoomByWheel(int32_t wheelDelta, bool& changed)
{
	if (wheelDelta == 0) {
		changed = false;
		return false;
	}

	return ZoomByNotches(static_cast<float>(wheelDelta) / 120.0f, changed);
}

bool SC4CameraController::ZoomByNotches(float wheelNotches, bool& changed)
{
	changed = false;

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
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

		targetMagnification = std::clamp(targetMagnification, 0.001f, 10.0f);

		while (targetMagnification > 2.0f && targetZoom < maxZoom) {
			targetMagnification *= 0.5f;
			++targetZoom;
		}

		while (targetMagnification < 0.5f && targetZoom > minZoom) {
			targetMagnification *= 2.0f;
			--targetZoom;
		}

		float targetContinuousZoom = static_cast<float>(targetZoom) + std::log2((std::max)(targetMagnification, 0.001f));
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

		cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
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

		cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			return false;
		}

		// Zoom never owns orientation. Reapply the player's current pitch and yaw
		// after SC4 changes native zoom stages, which may otherwise reload its
		// per-stage angle tables.
		ApplyPitchOverride(currentPitch);
		ApplyYawOverride(currentYaw);
		cameraControl->pitch = currentPitch;
		cameraControl->yaw = currentYaw;
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

bool SC4CameraController::ForceFullRedraw()
{
	return WithRenderer([](cISC43DRender* renderer) {
		return renderer->ForceFullRedraw();
	});
}

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

		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
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

bool SC4CameraController::ClearCameraDumpConfirmation() const
{
	return WithView3D([](cISC4View3DWin* view3D) {
		return view3D->ClearCursorText(kCameraDumpCursorTextId);
	});
}

SC4CameraControlLayout* SC4CameraController::GetActiveCameraControl()
{
	SC4CameraControlLayout* cameraControl = nullptr;

	WithRenderer([&](cISC43DRender* renderer) {
		cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		return cameraControl != nullptr;
	});

	return cameraControl;
}

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

float SC4CameraController::SanitizePitch(float pitch)
{
	return std::clamp(pitch, kMinPitch, kMaxPitch);
}

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

void SC4CameraController::SyncAngles(const SC4CameraControlLayout& cameraControl)
{
	if (anglesInitialized) {
		return;
	}

	currentPitch = SanitizePitch(cameraControl.pitch);
	currentYaw = SanitizeYaw(cameraControl.yaw);
	anglesInitialized = true;
}

void SC4CameraController::RestoreRotationAnchor(SC4CameraControlLayout& cameraControl) const
{
	if (!rotationGestureActive) {
		return;
	}

	cameraControl.viewTargetPosition = rotationViewTarget;
	cameraControl.baseTargetForRotation = rotationBaseTarget;
	cameraControl.viewTargetVelocity = {};
}

void SC4CameraController::RestoreNativeAngleTables()
{
	for (size_t i = 0; i < kZoomCount; i++) {
		OverwriteMemoryFloat(kPitchAddress1 + (i * sizeof(float)), kNativePitchByZoom[i]);
		OverwriteMemoryFloat(kPitchAddress2 + (i * sizeof(float)), kNativePitchByZoom[i]);
		OverwriteMemoryFloat(kYawAddress1 + (i * sizeof(float)), kDefaultYaw);
		OverwriteMemoryFloat(kYawAddress2 + (i * sizeof(float)), kDefaultYaw);
	}
	OverwriteMemoryFloat(kYawAddress0, kDefaultYaw);
	Logger::GetInstance().WriteLine(LogLevel::Info, "Camera Reset: restored native global pitch/yaw tables.");
}

void SC4CameraController::ApplyPitchOverride(float pitch)
{
	pitch = SanitizePitch(pitch);

	for (size_t i = 0; i < kZoomCount; i++) {
		OverwriteMemoryFloat(kPitchAddress1 + (i * sizeof(float)), pitch);
		OverwriteMemoryFloat(kPitchAddress2 + (i * sizeof(float)), pitch);
	}
}

void SC4CameraController::ApplyYawOverride(float yaw)
{
	yaw = SanitizeYaw(yaw);

	OverwriteMemoryFloat(kYawAddress0, yaw);

	for (size_t i = 0; i < kZoomCount; i++) {
		OverwriteMemoryFloat(kYawAddress1 + (i * sizeof(float)), yaw);
		OverwriteMemoryFloat(kYawAddress2 + (i * sizeof(float)), yaw);
	}
}

void SC4CameraController::OverwriteMemoryFloat(uintptr_t address, float value)
{
	DWORD oldProtect = 0;

	if (VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		*reinterpret_cast<float*>(address) = value;

		DWORD ignored = 0;
		VirtualProtect(reinterpret_cast<void*>(address), sizeof(value), oldProtect, &ignored);
	}
	else {
		char hexAddr[32];
		sprintf_s(hexAddr, sizeof(hexAddr), "0x%X", address);
		Logger::GetInstance().WriteLine(LogLevel::Error, std::string("VirtualProtect failed at ") + hexAddr);
	}
}
