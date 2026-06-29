#pragma once

#include "cS3DVector2.h"
#include "cS3DVector3.h"

#include <cstddef>
#include <cstdint>

struct SC4Float4
{
	float fX;
	float fY;
	float fZ;
	float fW;
};

// Reconstructed from the Windows x86 641 binary and cross-checked against the
// symbolized Mac x86 binary. The names are intended for practical DLL-side use.
struct SC4CameraControlLayout
{
	void* vtable;                               // 0x000
	uint8_t unk04;                              // 0x004
	uint8_t unk05;                              // 0x005
	uint8_t pad06[2];                           // 0x006
	int32_t refCount;                           // 0x008
	void* camera;                               // 0x00C cS3DCamera*
	cS3DVector3 cameraPlaneOrigin;              // 0x010
	cS3DVector3 baseTargetForRotation;          // 0x01C
	SC4Float4 unk28;                            // 0x028
	cS3DVector3 cameraPosition;                 // 0x038
	cS3DVector3 viewTargetPosition;             // 0x044
	cS3DVector3 viewTargetVelocity;             // 0x050
	cS3DVector3 groundRayDirection;             // 0x05C
	cS3DVector3 cachedViewXformA;               // 0x068
	cS3DVector3 cachedViewXformB;               // 0x074
	float groundPlaneEq[4];                     // 0x080
	float projectedCameraPlaneX;                // 0x090
	float projectedCameraPlaneY;                // 0x094
	float projectedScreenishX;                  // 0x098
	float projectedScreenishY;                  // 0x09C
	float prevProjectedCameraPlaneX;            // 0x0A0
	float prevProjectedCameraPlaneY;            // 0x0A4
	float prevProjectedScreenishX;              // 0x0A8
	float prevProjectedScreenishY;              // 0x0AC
	float viewportOffsetX;                      // 0x0B0
	float viewportOffsetY;                      // 0x0B4
	float zoomStepWorld[5];                     // 0x0B8
	uint32_t unkCC;                             // 0x0CC
	uint32_t unkD0;                             // 0x0D0
	uint32_t unkD4;                             // 0x0D4
	cS3DVector3 boundsA;                        // 0x0D8
	cS3DVector3 boundsB;                        // 0x0E4
	float customMagnification;                  // 0x0F0
	float minZoom;                              // 0x0F4
	float maxZoomLevelWithoutCustom;            // 0x0F8
	float maxZoom;                              // 0x0FC
	int32_t prevZoom;                           // 0x100
	int32_t prevRotation;                       // 0x104
	int32_t zoom;                               // 0x108
	int32_t rotation;                           // 0x10C
	int32_t postZoom;                           // 0x110
	int32_t postRotation;                       // 0x114
	float yaw;                                  // 0x118
	float pitch;                                // 0x11C
	float cameraDistance;                       // 0x120
	float nearClip;                             // 0x124
	float farClip;                              // 0x128
	int32_t viewportWidth;                      // 0x12C
	int32_t viewportHeight;                     // 0x130
	float orthoScale;                           // 0x134
	float invOrthoScale;                        // 0x138
	cS3DVector3 cameraOffset;                   // 0x13C
	uint8_t scrollBoundsEnabled;                // 0x148
	uint8_t pad149[3];                          // 0x149
	cS3DVector2* scrollBoundsBegin;             // 0x14C
	cS3DVector2* scrollBoundsEnd;               // 0x150
	cS3DVector2* scrollBoundsCapacityEnd;       // 0x154
	float citySizeX;                            // 0x158
	float citySizeZ;                            // 0x15C
};

static_assert(offsetof(SC4CameraControlLayout, yaw) == 0x118);
static_assert(offsetof(SC4CameraControlLayout, pitch) == 0x11C);
static_assert(sizeof(SC4CameraControlLayout) == 0x160);

// The native camera values observed before this plugin applies any free-camera
// rotation or continuous zoom changes. Pointer-owned and transient fields from
// SC4CameraControlLayout are deliberately excluded.
struct SC4NativeCameraState
{
	cS3DVector3 cameraPosition;
	cS3DVector3 viewTargetPosition;
	cS3DVector3 baseTargetForRotation;
	cS3DVector3 cameraOffset;
	float customMagnification;
	int32_t prevZoom;
	int32_t prevRotation;
	int32_t zoom;
	int32_t rotation;
	int32_t postZoom;
	int32_t postRotation;
	float yaw;
	float pitch;
	float cameraDistance;
	float nearClip;
	float farClip;
	float orthoScale;
	float invOrthoScale;
};

class SC4CameraController
{
public:
	static constexpr float kPi = 3.14159265358979323846f;
	static constexpr float kDefaultPitch = 0.52359879f;
	static constexpr float kDefaultYaw = -0.39269909f;
	static constexpr float kMinPitch = 0.261f;
	static constexpr float kMaxPitch = 1.483f;

	SC4CameraController();

	void Reset();
	bool BeginRotationGesture();
	void EndRotationGesture();

	bool ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw);
	bool AdjustScrollForCityBounds(
		float directionAngle,
		float speed,
		float& adjustedDirectionAngle,
		bool& blocked,
		const char* source);
	bool EnsureTargetNearCityCenterIfOutOfBounds(const char* source);
	bool ZoomByWheel(int32_t wheelDelta, bool& changed);
	bool ForceFullRedraw();
	bool DumpCameraInfo(const char* reason) const;
	bool ShowCameraDumpConfirmation() const;
	bool ClearCameraDumpConfirmation() const;
	bool HasNativeCameraState() const;
	const SC4NativeCameraState& GetNativeCameraState() const;
	bool BeginSavePreviewNormalization();
	void EndSavePreviewNormalization();
	void AbandonSavePreviewNormalization();
	bool IsSavePreviewNormalizationActive() const;

private:
	bool EnsureNativeCameraStateCaptured(const SC4CameraControlLayout& cameraControl);
	static SC4CameraControlLayout* GetActiveCameraControl();
	static bool Refresh(SC4CameraControlLayout& cameraControl);
	static float SanitizePitch(float pitch);
	static float SanitizeYaw(float yaw);
	static void RestoreNativeAngleTables();
	static void ApplyPitchOverride(float pitch);
	static void ApplyYawOverride(float yaw);
	static void OverwriteMemoryFloat(uintptr_t address, float value);
	bool ZoomByNotches(float wheelNotches, bool& changed);
	void SyncAngles(const SC4CameraControlLayout& cameraControl);
	void RestoreRotationAnchor(SC4CameraControlLayout& cameraControl) const;

	float currentPitch;
	float currentYaw;
	bool anglesInitialized;
	bool rotationGestureActive;
	float rotationOrthoScale;
	cS3DVector3 rotationViewTarget;
	cS3DVector3 rotationBaseTarget;
	bool nativeCameraStateCaptured;
	SC4NativeCameraState nativeCameraState;
	bool savePreviewNormalizationActive;
	float savedPitchTables[2][5];
	float savedYawInstruction;
	float savedYawTables[2][5];
	float savedPreviewPitch;
	float savedPreviewYaw;
	cS3DVector3 savedPreviewViewTarget;
	cS3DVector3 savedPreviewBaseTarget;
};
