#pragma once

#include "cS3DVector2.h"
#include "cS3DVector3.h"

#include <cstddef>
#include <cstdint>

class cISC43DRender;

// Four-float value retained where the native camera layout exposes one.
struct SC4Float4
{
	// First component of the native four-float value.
	float fX;
	// Second component of the native four-float value.
	float fY;
	// Third component of the native four-float value.
	float fZ;
	// Fourth component of the native four-float value.
	float fW;
};

// Reconstructed from the Windows x86 641 binary and cross-checked against the
// symbolized Mac x86 binary. The names are intended for practical DLL-side use.
struct SC4CameraControlLayout
{
	void* vtable;                               // 0x000 Native camera-control virtual table.
	uint8_t unk04;                              // 0x004 Unidentified native field.
	uint8_t unk05;                              // 0x005 Unidentified native field.
	uint8_t pad06[2];                           // 0x006 Padding retained for binary layout.
	int32_t refCount;                           // 0x008 Native reference count.
	void* camera;                               // 0x00C Native cS3DCamera pointer.
	cS3DVector3 cameraPlaneOrigin;              // 0x010 Camera plane origin used by native projection.
	cS3DVector3 baseTargetForRotation;          // 0x01C Native target used as the rotation anchor.
	SC4Float4 unk28;                            // 0x028 Unidentified native four-float value.
	cS3DVector3 cameraPosition;                 // 0x038 Current native camera position.
	cS3DVector3 viewTargetPosition;             // 0x044 Current native camera target position.
	cS3DVector3 viewTargetVelocity;             // 0x050 Native target velocity used while scrolling.
	cS3DVector3 groundRayDirection;             // 0x05C Native camera ray direction toward the ground plane.
	cS3DVector3 cachedViewXformA;               // 0x068 Native cached view-transform component.
	cS3DVector3 cachedViewXformB;               // 0x074 Native cached view-transform component.
	float groundPlaneEq[4];                     // 0x080 Native ground-plane equation coefficients.
	float projectedCameraPlaneX;                // 0x090 Native projected camera-plane X coordinate.
	float projectedCameraPlaneY;                // 0x094 Native projected camera-plane Y coordinate.
	float projectedScreenishX;                  // 0x098 Native projected screen-space-like X value.
	float projectedScreenishY;                  // 0x09C Native projected screen-space-like Y value.
	float prevProjectedCameraPlaneX;            // 0x0A0 Previous projected camera-plane X coordinate.
	float prevProjectedCameraPlaneY;            // 0x0A4 Previous projected camera-plane Y coordinate.
	float prevProjectedScreenishX;              // 0x0A8 Previous projected screen-space-like X value.
	float prevProjectedScreenishY;              // 0x0AC Previous projected screen-space-like Y value.
	float viewportOffsetX;                      // 0x0B0 Native viewport X offset.
	float viewportOffsetY;                      // 0x0B4 Native viewport Y offset.
	float zoomStepWorld[5];                     // 0x0B8 Native world-space zoom steps.
	uint32_t unkCC;                             // 0x0CC Unidentified native field.
	uint32_t unkD0;                             // 0x0D0 Unidentified native field.
	uint32_t unkD4;                             // 0x0D4 Unidentified native field.
	cS3DVector3 boundsA;                        // 0x0D8 Native camera-bounds endpoint A.
	cS3DVector3 boundsB;                        // 0x0E4 Native camera-bounds endpoint B.
	float customMagnification;                  // 0x0F0 Continuous magnification within the current zoom stage.
	float minZoom;                              // 0x0F4 Lowest native zoom-stage value.
	float maxZoomLevelWithoutCustom;            // 0x0F8 Highest native zoom stage before custom magnification.
	float maxZoom;                              // 0x0FC Highest native zoom-stage value.
	int32_t prevZoom;                           // 0x100 Previous native zoom-stage value.
	int32_t prevRotation;                       // 0x104 Previous native quarter-turn rotation.
	int32_t zoom;                               // 0x108 Current native zoom-stage value.
	int32_t rotation;                           // 0x10C Current native quarter-turn rotation.
	int32_t postZoom;                           // 0x110 Pending native zoom-stage value.
	int32_t postRotation;                       // 0x114 Pending native quarter-turn rotation.
	float yaw;                                  // 0x118 Current native yaw in radians.
	float pitch;                                // 0x11C Current native pitch in radians.
	float cameraDistance;                       // 0x120 Native camera-to-target distance.
	float nearClip;                             // 0x124 Native near clipping distance.
	float farClip;                              // 0x128 Native far clipping distance.
	int32_t viewportWidth;                      // 0x12C Native viewport width in pixels.
	int32_t viewportHeight;                     // 0x130 Native viewport height in pixels.
	float orthoScale;                           // 0x134 Native orthographic projection scale.
	float invOrthoScale;                        // 0x138 Reciprocal native orthographic projection scale.
	cS3DVector3 cameraOffset;                   // 0x13C Native camera offset from its base transform.
	uint8_t scrollBoundsEnabled;                // 0x148 Whether native scroll bounds are enabled.
	uint8_t pad149[3];                          // 0x149 Padding retained for binary layout.
	cS3DVector2* scrollBoundsBegin;             // 0x14C Start of the native scroll-bounds list.
	cS3DVector2* scrollBoundsEnd;               // 0x150 End of the native scroll-bounds list.
	cS3DVector2* scrollBoundsCapacityEnd;       // 0x154 Capacity end of the native scroll-bounds list.
	float citySizeX;                            // 0x158 City extent along the X axis.
	float citySizeZ;                            // 0x15C City extent along the Z axis.
};

static_assert(offsetof(SC4CameraControlLayout, yaw) == 0x118);
static_assert(offsetof(SC4CameraControlLayout, pitch) == 0x11C);
static_assert(sizeof(SC4CameraControlLayout) == 0x160);

// The native camera values observed before this plugin applies any free-camera
// rotation or continuous zoom changes. Pointer-owned and transient fields from
// SC4CameraControlLayout are deliberately excluded.
struct SC4NativeCameraState
{
	// Native camera-plane origin captured before Modern Camera changes.
	cS3DVector3 cameraPlaneOrigin;
	// Native camera position captured before Modern Camera changes.
	cS3DVector3 cameraPosition;
	// Native view target captured before Modern Camera changes.
	cS3DVector3 viewTargetPosition;
	// Native rotation target captured before Modern Camera changes.
	cS3DVector3 baseTargetForRotation;
	// Native camera offset captured before Modern Camera changes.
	cS3DVector3 cameraOffset;
	// Native continuous magnification captured before Modern Camera changes.
	float customMagnification;
	// Previous native zoom stage.
	int32_t prevZoom;
	// Previous native quarter-turn rotation.
	int32_t prevRotation;
	// Current native zoom stage.
	int32_t zoom;
	// Current native quarter-turn rotation.
	int32_t rotation;
	// Pending native zoom stage.
	int32_t postZoom;
	// Pending native quarter-turn rotation.
	int32_t postRotation;
	// Native yaw in radians.
	float yaw;
	// Native pitch in radians.
	float pitch;
	// Native camera-to-target distance.
	float cameraDistance;
	// Native near clipping distance.
	float nearClip;
	// Native far clipping distance.
	float farClip;
	// Native orthographic projection scale.
	float orthoScale;
	// Reciprocal native orthographic projection scale.
	float invOrthoScale;
};

// Applies Modern Camera rotation, zoom, bounds, and native-state restoration.
class SC4CameraController
{
public:
	// Mathematical constant used when converting native camera angles.
	static constexpr float kPi = 3.14159265358979323846f;
	// Default pitch applied when no valid native angle is available.
	static constexpr float kDefaultPitch = 0.52359879f;
	// Default yaw applied when no valid native angle is available.
	static constexpr float kDefaultYaw = -0.39269909f;
	// Lowest pitch accepted by Modern Camera.
	static constexpr float kMinPitch = 0.261f;
	// Highest pitch accepted by Modern Camera.
	static constexpr float kMaxPitch = 1.483f;

	// Creates a controller with no captured native camera state.
	SC4CameraController();

	// Clears retained state and lets the next camera update resynchronize angles.
	void Reset();
	// Captures the current rotation anchor before a free-camera rotation gesture begins.
	bool BeginRotationGesture();
	// Releases the rotation anchor retained for the current gesture.
	void EndRotationGesture();

	// Applies pitch and optional yaw deltas to the active native camera control.
	bool ApplyDelta(float pitchDelta, float yawDelta, bool updateYaw);
	// Clamps a native scroll request to the loaded city's bounds.
	bool AdjustScrollForCityBounds(
		float directionAngle,
		float speed,
		float& adjustedDirectionAngle,
		bool& blocked,
		const char* source);
	// Clamps a scroll request using a renderer already resolved by the caller.
	bool AdjustScrollForCityBounds(
		cISC43DRender* renderer,
		float directionAngle,
		float speed,
		float& adjustedDirectionAngle,
		bool& blocked,
		const char* source);
	// Switches to the temporary Classic camera angle while Alt is held.
	bool BeginClassicAngleHold(const char* source);
	// Restores Modern Camera angles after the temporary Classic-angle hold.
	bool EndClassicAngleHold(const char* source);
	// Returns whether temporary Classic-angle mode is active.
	bool IsClassicAngleHoldActive() const;
	// Performs one native Classic camera quarter-turn for an Alt+Q or Alt+E press.
	bool RotateClassicStep(bool left, const char* source);
	// Re-centers a native target that falls outside verified city bounds.
	bool EnsureTargetNearCityCenterIfOutOfBounds(const char* source);
	// Applies a mouse-wheel delta and reports whether the camera zoom changed.
	bool ZoomByWheel(int32_t wheelDelta, bool& changed);
	// Requests the active SC4 renderer to perform a full redraw.
	bool ForceFullRedraw();
	// Writes the active camera state to the diagnostic log.
	bool DumpCameraInfo(const char* reason) const;
	// Shows the temporary camera-dump confirmation UI.
	bool ShowCameraDumpConfirmation() const;
	// Clears the temporary camera-dump confirmation UI.
	bool ClearCameraDumpConfirmation() const;
	/// <summary>
	/// Shows a temporary confirmation after a bookmark save or recall.
	/// </summary>
	/// <param name="saved">True for a save acknowledgement; false for a recall acknowledgement.</param>
	/// <param name="slot">Zero-based bookmark slot that completed the command.</param>
	/// <returns>True when SC4 accepted the cursor-text entry.</returns>
	bool ShowCameraBookmarkConfirmation(bool saved, uint32_t slot) const;
	// Clears the temporary bookmark confirmation UI.
	bool ClearCameraBookmarkConfirmation() const;
	// Returns whether a restorable native camera state has been captured.
	bool HasNativeCameraState() const;
	// Returns the native camera snapshot retained by this controller.
	const SC4NativeCameraState& GetNativeCameraState() const;
	// Captures a native camera state for later restoration.
	bool CaptureNativeCameraState(const char* source);
	// Restores the previously captured native camera state.
	bool RestoreNativeCameraState(const char* source);
	// Captures the active view without changing the controller's native-reset snapshot.
	bool CaptureCurrentCameraState(SC4NativeCameraState& state, const char* source) const;
	// Restores a saved full view. Its target is clamped to current city bounds,
	// with each dependent world-space camera value translated by the same delta.
	bool RestoreCameraState(SC4NativeCameraState state, const char* source);
	// Normalizes camera state before SC4 creates a save-preview image.
	bool BeginSavePreviewNormalization();
	// Restores Modern Camera state after a save-preview normalization.
	void EndSavePreviewNormalization();
	// Discards a pending save-preview normalization without restoring it.
	void AbandonSavePreviewNormalization();
	// Returns whether save-preview normalization has retained restorable state.
	bool IsSavePreviewNormalizationActive() const;

private:
	// Captures native state once for the supplied active camera control.
	bool EnsureNativeCameraStateCaptured(const SC4CameraControlLayout& cameraControl);
	// Refreshes SC4's derived camera state after direct value changes.
	static bool Refresh(SC4CameraControlLayout& cameraControl);
	// Clamps invalid pitch values to the supported Modern Camera range.
	static float SanitizePitch(float pitch);
	// Normalizes yaw to the range expected by the native camera tables.
	static float SanitizeYaw(float yaw);
	// Writes the plugin's baseline pitch and yaw values to the native tables.
	static void WriteNativeCameraAngleTables();
	// Restores the plugin's baseline pitch and yaw tables and records the action.
	static void RestoreNativeAngleTables();
	// Restores angle tables captured before save-preview normalization.
	void RestoreSavedCameraAngleTables();
	// Restores the camera-control fields saved before save-preview normalization.
	bool RestoreSavedPreviewCameraState(SC4CameraControlLayout& cameraControl);
	// Writes pitch and yaw overrides together, then updates the supplied control fields.
	static void ApplyAngleOverrides(
		SC4CameraControlLayout& cameraControl,
		float pitch,
		float yaw);
	// Writes a pitch override to the native camera tables.
	static void ApplyPitchOverride(float pitch);
	// Converts wheel notches into a bounded native zoom update.
	bool ZoomByNotches(float wheelNotches, bool& changed);
	// Copies native camera angles into Modern Camera's tracked state.
	void SyncAngles(const SC4CameraControlLayout& cameraControl);
	// Restores camera-target fields captured at the start of rotation.
	void RestoreRotationAnchor(SC4CameraControlLayout& cameraControl) const;

	// Last valid Modern Camera pitch in radians.
	float currentPitch;
	// Last valid Modern Camera yaw in radians.
	float currentYaw;
	// Indicates whether currentPitch and currentYaw came from native state.
	bool anglesInitialized;
	// Indicates that temporary Classic-angle mode is currently active.
	bool classicAngleHoldActive;
	// Modern Camera pitch saved before entering temporary Classic-angle mode.
	float classicAngleHoldPitch;
	// Modern Camera yaw saved before entering temporary Classic-angle mode.
	float classicAngleHoldYaw;
	// Native rotation index saved before entering temporary Classic-angle mode.
	int32_t classicAngleHoldRotation;
	// Quarter-turn yaw selected for temporary Classic-angle mode.
	float classicAngleHoldSnapYaw;
	// Native rotation index matching classicAngleHoldSnapYaw.
	int32_t classicAngleHoldSnapRotation;
	// Indicates that rotationViewTarget and rotationBaseTarget are valid.
	bool rotationGestureActive;
	// Orthographic scale captured when the current rotation gesture began.
	float rotationOrthoScale;
	// Native target position captured when the current rotation gesture began.
	cS3DVector3 rotationViewTarget;
	// Native rotation base captured when the current rotation gesture began.
	cS3DVector3 rotationBaseTarget;
	// Indicates whether nativeCameraState contains a restorable state.
	bool nativeCameraStateCaptured;
	// First native camera snapshot captured by this controller.
	SC4NativeCameraState nativeCameraState;
	// Indicates whether save-preview state is currently retained.
	bool savePreviewNormalizationActive;
	// Native pitch tables captured at the start of save-preview normalization.
	float savedPitchTables[2][5];
	// Native yaw instruction value captured at the start of save-preview normalization.
	float savedYawInstruction;
	// Native yaw tables captured at the start of save-preview normalization.
	float savedYawTables[2][5];
	// Modern Camera pitch saved before save-preview normalization.
	float savedPreviewPitch;
	// Modern Camera yaw saved before save-preview normalization.
	float savedPreviewYaw;
	// Native view target saved before save-preview normalization.
	cS3DVector3 savedPreviewViewTarget;
	// Native rotation base saved before save-preview normalization.
	cS3DVector3 savedPreviewBaseTarget;
};
