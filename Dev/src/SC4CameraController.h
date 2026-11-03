#pragma once

#include "SC4BuildingComposer.h"
#include "SC4CityIcons.h"
#include "cS3DVector2.h"
#include "cS3DVector3.h"

#include <array>
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

// Player-facing outcome of one numbered bookmark command.
enum class CameraBookmarkNotice : uint8_t
{
	// The current view was stored in the requested slot.
	Saved,
	// The camera moved to the requested slot.
	Recalled,
	// The requested slot has no stored view.
	Empty,
	// The current view could not be stored in the requested slot.
	SaveFailed,
	// The stored view could not be applied.
	RecallFailed,
	// The background writer could not update the bookmark file.
	StorageFailed,
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
	// Saves the Modern view and moves to the nearest Classic angle and zoom while Alt is held.
	bool BeginClassicAngleHold(const char* source);
	/// <summary>
	/// Returns to the Modern view saved when the temporary Classic-angle hold began.
	/// </summary>
	/// <param name="source">Label that identifies the caller in the log.</param>
	/// <param name="animate">True to move there over the following frames, false to arrive now.</param>
	/// <returns>True when the return started or completed.</returns>
	bool EndClassicAngleHold(const char* source, bool animate);
	/// <summary>
	/// Ends the temporary Classic-angle hold and stays on the view it reached, with a small
	/// drop of the camera as feedback.
	/// </summary>
	/// <param name="source">Label that identifies the caller in the log.</param>
	/// <returns>True when the drop started.</returns>
	bool DropClassicAngleHold(const char* source);
	// Returns whether temporary Classic-angle mode is active.
	bool IsClassicAngleHoldActive() const;
	// Starts one Classic quarter turn for an Alt+Q or Alt+E press.
	bool RotateClassicStep(bool left, const char* source);
	/// <summary>
	/// Moves to the next Classic zoom level for each whole wheel notch during the Alt hold.
	/// </summary>
	/// <param name="wheelNotches">Wheel movement in notches; positive zooms in.</param>
	/// <param name="changed">Receives whether a move to another zoom level started.</param>
	/// <returns>True when the hold consumed the wheel input.</returns>
	bool StepClassicZoom(float wheelNotches, bool& changed);
	/// <summary>
	/// Advances the move between the Modern and Classic views by one camera frame.
	/// </summary>
	/// <param name="elapsedSeconds">Time since the previous camera frame.</param>
	void AdvanceClassicAngleTransition(float elapsedSeconds);
	/// <summary>
	/// Completes a move between the Modern and Classic views at once, for callers that
	/// need the view to be final, such as a save or a bookmark.
	/// </summary>
	void FinishClassicAngleTransition();
	// Re-centers a native target that falls outside verified city bounds.
	bool EnsureTargetNearCityCenterIfOutOfBounds(const char* source);
	// Applies a sensitivity-adjusted wheel delta and reports whether the camera zoom changed.
	bool ZoomByWheel(float wheelDelta, bool& changed);
	// Requests the active SC4 renderer to perform a full redraw.
	bool ForceFullRedraw();
	/// <summary>
	/// Completes the native notifications, render-grid rebuild, and redraw requested since the previous frame.
	/// </summary>
	void FlushPendingViewUpdates();
	/// <summary>
	/// Returns the multiplier that keeps keyboard panning at a constant on-screen speed within a zoom stage.
	/// </summary>
	/// <returns>The reciprocal of the current magnification, or 1 when no usable camera is available.</returns>
	float GetPanSpeedZoomScale() const;
	// Writes the active camera state to the diagnostic log.
	bool DumpCameraInfo(const char* reason) const;
	// Shows the temporary camera-dump confirmation UI.
	bool ShowCameraDumpConfirmation() const;
	// Clears the temporary camera-dump confirmation UI.
	bool ClearCameraDumpConfirmation() const;
	/// <summary>
	/// Shows a temporary message describing the outcome of a bookmark command.
	/// </summary>
	/// <param name="notice">Outcome to report to the player.</param>
	/// <param name="slot">Zero-based bookmark slot. Ignored for storage failures.</param>
	/// <returns>True when SC4 accepted the cursor-text entry.</returns>
	bool ShowCameraBookmarkNotice(CameraBookmarkNotice notice, uint32_t slot) const;
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
	/// <summary>
	/// Returns the list of city icons this controller keeps facing the camera.
	/// </summary>
	/// <returns>The tracked city icons.</returns>
	SC4CityIcons& GetCityIcons();
	/// <summary>
	/// Returns the object that draws baked models as complete buildings at free camera angles.
	/// </summary>
	/// <returns>The building composer.</returns>
	SC4BuildingComposer& GetBuildingComposer();

private:
	// One complete view the Alt hold can save or move to. The focus is not part of it,
	// because switching views never moves the focus.
	struct ClassicAngleView
	{
		// Native quarter-turn rotation.
		int32_t rotation;
		// Yaw in radians.
		float yaw;
		// Pitch in radians.
		float pitch;
		// Native zoom stage.
		int32_t zoom;
		// Custom magnification within the zoom stage.
		float magnification;
	};

	// Captures native state once for the supplied active camera control.
	bool EnsureNativeCameraStateCaptured(const SC4CameraControlLayout& cameraControl);
	// Refreshes SC4's derived camera state after direct value changes without notifying subscribers.
	static bool Refresh(SC4CameraControlLayout& cameraControl);
	/// <summary>
	/// Recomputes SC4's derived camera state from the current fields without notifying
	/// subscribers and without the target clamp.
	/// </summary>
	/// <param name="cameraControl">Camera control to recompute.</param>
	/// <returns>True when the native update ran.</returns>
	static bool RecomputeCamera(SC4CameraControlLayout& cameraControl);
	/// <summary>
	/// Puts the camera in one view of an Alt-hold move, keeping the focus on the same point
	/// of the map.
	/// </summary>
	/// <param name="renderer">Active renderer.</param>
	/// <param name="view">View to apply.</param>
	/// <returns>True when the camera now uses the requested view.</returns>
	bool ApplyClassicAngleView(cISC43DRender* renderer, const ClassicAngleView& view);
	/// <summary>
	/// Starts moving from the camera's current view to a new one.
	/// </summary>
	/// <param name="cameraControl">Camera control holding the current view.</param>
	/// <param name="target">View to end on.</param>
	/// <param name="yawTravel">Signed turn to make on the way, in radians.</param>
	void StartClassicAngleTransition(
		const SC4CameraControlLayout& cameraControl,
		const ClassicAngleView& target,
		float yawTravel);
	/// <summary>
	/// Returns the turn the current Alt-hold move has left to make.
	/// </summary>
	/// <returns>The signed turn in radians, or 0 when no move is in progress.</returns>
	float GetClassicTransitionRemainingYaw() const;
	/// <summary>
	/// Ends the current Alt-hold move on the exact target view and tells native subscribers.
	/// </summary>
	/// <param name="renderer">Active renderer.</param>
	/// <returns>True when the camera now uses the target view.</returns>
	bool CompleteClassicAngleTransition(cISC43DRender* renderer);
	/// <summary>
	/// Starts Classic quarter turns from wherever the hold is heading.
	/// </summary>
	/// <param name="turns">Signed number of quarter turns.</param>
	/// <param name="source">Label that identifies the input in the log.</param>
	/// <returns>True when the turn started.</returns>
	bool TurnClassicView(int32_t turns, const char* source);
	/// <summary>
	/// Requests one native notification, clamp, and render-grid rebuild after a discrete view change.
	/// </summary>
	/// <param name="keepViewTarget">True when the native clamp must not move the focus.</param>
	void RequestViewSettle(bool keepViewTarget = false);
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
	/// <summary>
	/// Rotates the city icons to match the current camera. Runs before the render-grid
	/// rebuild, which reads their bounds.
	/// </summary>
	void OrientCityIcons();
	/// <summary>
	/// Tells the building composer about the current view. Runs before the pending redraw is read,
	/// so a redraw it asks for happens in the same frame.
	/// </summary>
	void UpdateBuildingComposer();

	// City icons kept facing the camera.
	SC4CityIcons cityIcons;
	// Draws baked models as complete buildings at free camera angles.
	SC4BuildingComposer buildingComposer;
	// Camera position, view target, magnification, yaw, and pitch at the last building composer
	// update, for telling whether the view moved since.
	std::array<float, 9> lastComposerView{};
	// Last valid Modern Camera pitch in radians.
	float currentPitch;
	// Last valid Modern Camera yaw in radians.
	float currentYaw;
	// Indicates whether currentPitch and currentYaw came from native state.
	bool anglesInitialized;
	// Indicates that temporary Classic-angle mode is currently active.
	bool classicAngleHoldActive;
	// Modern view saved before entering temporary Classic-angle mode.
	ClassicAngleView classicAngleHoldSavedView;
	// Classic zoom level the hold is using or heading to.
	int32_t classicAngleHoldZoomLevel;
	// M3 yaw accumulated during the hold, in radians.
	float classicAngleHoldDragYaw;
	// Quarter turns already made from classicAngleHoldDragYaw.
	int32_t classicAngleHoldDragTurns;
	// Wheel movement during the hold that has not yet made up a whole notch.
	float classicAngleHoldWheelNotches;
	// Indicates that the camera is moving toward classicTransitionTarget.
	bool classicTransitionActive;
	// Indicates that the current move adds the small drop played when Alt is let go.
	bool classicTransitionDrop;
	// Time spent on the current move, in seconds.
	float classicTransitionElapsed;
	// Eased progress of the last frame of the current move.
	float classicTransitionEased;
	// Azimuth the current move started from, in radians.
	float classicTransitionFromAzimuth;
	// Signed turn the current move makes, in radians.
	float classicTransitionYawTravel;
	// Pitch the current move started from, in radians.
	float classicTransitionFromPitch;
	// On-screen zoom the current move started from.
	float classicTransitionFromZoomLevel;
	// On-screen zoom the current move ends on.
	float classicTransitionToZoomLevel;
	// Native zoom stage the current move started from.
	int32_t classicTransitionFromZoom;
	// Exact view the current move ends on.
	ClassicAngleView classicTransitionTarget;
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
	// Native yaw table captured at the start of save-preview normalization.
	float savedYawTable[5];
	// Modern Camera pitch saved before save-preview normalization.
	float savedPreviewPitch;
	// Modern Camera yaw saved before save-preview normalization.
	float savedPreviewYaw;
	// Native view target saved before save-preview normalization.
	cS3DVector3 savedPreviewViewTarget;
	// Native rotation base saved before save-preview normalization.
	cS3DVector3 savedPreviewBaseTarget;
	// Indicates that the active rotation gesture has changed the camera.
	bool rotationGestureChanged;
	// Indicates that native subscribers must be told about a completed view change.
	bool viewSettlePending;
	// Indicates that the pending view change is an Alt switch, which keeps its view target.
	bool viewSettleKeepsTarget;
	// Indicates that the renderer's camera-dependent view focus must be recalculated.
	bool cameraUpdatePending;
	// Indicates that pitch or yaw changed, so the orientation-dependent render grid is stale.
	bool gridRebuildPending;
	// Indicates that a full redraw is required.
	bool redrawPending;
};
