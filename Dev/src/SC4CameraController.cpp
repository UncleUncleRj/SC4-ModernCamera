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
	constexpr size_t kZoomCount = 5;
	constexpr float kNativeYawAnchor = -SC4CameraController::kPi * 0.125f;
	constexpr float kMaxNativeYawOffset = 35.0f * SC4CameraController::kPi / 180.0f;
	constexpr float kMinNativeYawOffset = kMaxNativeYawOffset - (SC4CameraController::kPi * 0.5f);

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
	  currentYaw(kDefaultYaw)
{
}

float SC4CameraController::GetPitch() const
{
	return currentPitch;
}

float SC4CameraController::GetYaw() const
{
	return currentYaw;
}

void SC4CameraController::Reset()
{
	currentPitch = kDefaultPitch;
	currentYaw = kDefaultYaw;
}

bool SC4CameraController::ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw)
{
	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control from renderer.");
			return false;
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

				cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
				if (!cameraControl) {
					Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Rotation Rebalance: camera control unavailable after SetRotation.");
					return false;
				}
			}
		}

		ApplyPitchOverride(currentPitch);

		if (updateYaw) {
			ApplyYawOverride(currentYaw);
			cameraControl->yaw = currentYaw;
		}

		cameraControl->pitch = currentPitch;

		return Refresh(*cameraControl);
	});
}

bool SC4CameraController::DollyByWheel(int32_t wheelDelta)
{
	if (wheelDelta == 0) {
		return false;
	}

	return WithRenderer([&](cISC43DRender* renderer) {
		SC4CameraControlLayout* cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		if (!cameraControl) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Failed to get SC4 camera control for dolly.");
			return false;
		}

		float targetMagnification = cameraControl->customMagnification
			* std::pow(kMagnificationScalePerWheelNotch, static_cast<float>(wheelDelta) / 120.0f);
		int32_t targetZoom = cameraControl->zoom;

		const float oldMagnification = cameraControl->customMagnification;
		const int32_t oldZoom = cameraControl->zoom;
		const int32_t minZoom = static_cast<int32_t>(std::floor(cameraControl->minZoom));
		const int32_t maxZoom = static_cast<int32_t>(std::floor(cameraControl->maxZoomLevelWithoutCustom));

		targetMagnification = std::clamp(targetMagnification, 0.001f, 10.0f);

		while (targetMagnification > 2.0f && targetZoom < maxZoom) {
			targetMagnification *= 0.5f;
			++targetZoom;
		}

		while (targetMagnification < 0.5f && targetZoom > minZoom) {
			targetMagnification *= 2.0f;
			--targetZoom;
		}

		if (targetZoom != oldZoom && !renderer->SetZoom(targetZoom)) {
			Logger::GetInstance().WriteLine(LogLevel::Warning, "Camera Dolly: renderer SetZoom failed.");
			return false;
		}

		cameraControl = reinterpret_cast<SC4CameraControlLayout*>(renderer->GetCameraControl());
		auto setCustomMagnification = GetSetCustomMagnification();

		if (!cameraControl || !setCustomMagnification) {
			Logger::GetInstance().WriteLine(LogLevel::Error, "Camera Dolly: custom magnification thunk unavailable.");
			return false;
		}

		const bool result = setCustomMagnification(cameraControl, std::clamp(targetMagnification, 0.001f, 10.0f));

		Logger::GetInstance().WriteLine(
			LogLevel::Info,
			"Camera Dolly: WheelDelta:" + std::to_string(wheelDelta)
			+ " OldZoom:" + std::to_string(oldZoom)
			+ " NewZoom:" + std::to_string(targetZoom)
			+ " OldMagnification:" + std::to_string(oldMagnification)
			+ " NewMagnification:" + std::to_string(targetMagnification)
			+ " Result:" + std::to_string(result ? 1 : 0));

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
		static cRZBaseString title("SC4-3DMouseCam");
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
