# Camera Zoom and Rotation Design

This document defines the intended camera behavior for GitHub issue
[#4](https://github.com/UncleUncleRj/SC4-3DMouseCam/issues/4). It incorporates
the original camera concept, play testing in SC4, and comparison testing with
other city-building camera systems.

## Core Principle

Zoom distance, pitch, and yaw are independent persistent camera state.

The mouse wheel changes zoom distance only. It must not select, restore, or
otherwise argue with the player's pitch or yaw. Middle-mouse drag changes pitch
and yaw at any zoom distance, and the selected orientation remains in effect
until the player changes it again.

This produces all desired view types without a forced zoom/pitch curve:

- Zooming in while top-down produces a close top-down view.
- Pitching toward the horizon and zooming in produces an angled or street-level
  view.
- Zooming out preserves that angled view.
- Rotating and then zooming preserves both pitch and yaw.

## Player Experience

### Top-down

- Shows the full city in a retro SimCity-style overview.
- May be used at any zoom distance.
- Horizontal M3 movement rotates the map.
- Vertical M3 movement can pitch out of top-down at any time.

### Angled

- Farther views prioritize city and infrastructure management.
- Closer views combine management with immersion.
- The player chooses the angle directly and zoom does not change it.

### Street level

- Prioritizes immersion rather than city management.
- Reached by choosing a low pitch and zooming toward the city.
- The player can still change pitch and yaw while zoomed in.

## Controls

### Mouse wheel

- Smoothly changes zoom distance toward or away from SC4's current screen-center
  focus.
- Preserves pitch and yaw exactly across continuous magnification changes and
  native SC4 zoom-stage changes.
- Retains the game's existing right-drag plus wheel behavior. SC4 remains
  responsible for moving its normal focus during that combined input.

### Middle mouse (M3) drag

- Horizontal movement changes yaw.
- Vertical movement changes pitch.
- Works at every zoom distance, including close and top-down views.
- Uses safe pitch limits to avoid singular, inverted, or invalid camera states.
- Does not require Shift or a separate free-look mode.

## Rotation Pivot and Anchor

At the beginning of an M3 rotation gesture, capture SC4's current valid ground
focus and freeze it until M3 is released. Pitch and yaw rotate the camera around
that pivot. This is the conventional stable orbit-camera model: the camera
position traces the arc while the ground anchor does not drift.

The initial implementation freezes both known target fields:

- `viewTargetPosition`
- `baseTargetForRotation`

It also suppresses target velocity during the gesture. This should prevent
inertial focus drift and give SC4 a stable logical point on playable terrain for
rendering and culling.

The physical camera should remain within the playable-map constraints already
enforced by SC4. Rotation near map boundaries requires runtime testing. If an
orbit would place the camera outside valid bounds, later work may clamp the
orbit radius or blend toward in-place rotation.

### Possible dual-anchor extension

SC4 may distinguish the visible lens target from the logical target used for
culling. If runtime investigation confirms that distinction, a future version
may use:

- A moving visual target derived from the requested pitch/yaw.
- A frozen or map-clamped logical ground anchor used by SC4's renderer.

For a stationary camera, the visible ground target for yaw would trace a circle
around the camera's ground projection. Its approximate radius on level terrain
would be `camera height / tan(downward pitch)`. This is not enabled until the
roles of the reconstructed camera fields are verified; moving the wrong target
could reintroduce drift, disappearing buildings, or void-rendering artifacts.

## Rendering and Culling

SC4 can hide buildings or produce artifacts when its camera/focus state points
toward the surrounding void. A stable valid ground pivot is the conservative
first defense against that behavior.

Anchor stability cannot be assumed to solve every culling problem. Native zoom
stage, renderer refresh, camera position, and an internal logical focus may all
participate. F8 camera dumps and gesture/zoom logging should be used to compare
healthy and broken states before assigning unverified meanings to fields.

## Current Implementation Checkpoint

- Wheel zoom uses continuous magnification across SC4's native zoom stages.
- Wheel zoom reapplies the player's current pitch and yaw after native stage
  changes.
- M3 horizontal drag changes yaw.
- M3 vertical drag changes pitch.
- M3 down freezes the current rotation pivot; M3 up releases it.
- M3 captures the current orthographic scale. During rotation, custom
  magnification counters SC4's square-to-diamond bounds-fit so the visible zoom
  does not bounce merely to keep every map corner inside the viewport.
- Native yaw uses a `[-70 degrees, +30 degrees]` hysteresis window. Building
  side meshes were observed disappearing when an exact 90-degree window mapped
  a healthy low-end orientation directly onto the opposite high-end boundary.
  The wider window delays that handoff and maps it into the safer interior of
  the adjacent bucket instead.
- Gesture anchor capture/release and every zoom update are logged.
- The experimental automatic pitch curve, vertical-M3 dolly zoom, and Shift
  free-look mode have been removed.

## Acceptance Criteria

- Zooming in and out never changes a player-selected pitch or yaw.
- Close top-down, far angled, close angled, and street-level views are all
  reachable and remain stable.
- M3 pitch/yaw works at every zoom distance without requiring Shift.
- Starting rotation does not shift the visible focus point.
- Repeated rotation does not accumulate anchor drift.
- Native right-drag plus wheel focus tracking continues to work.
- Zoom remains smooth when crossing native SC4 zoom-stage boundaries.
- Camera movement does not unnecessarily remove buildings or introduce void
  artifacts.
- No camera orientation can invert or enter an invalid singular state.

## Stabilization Summary

Runtime testing confirmed that SC4 expands its orthographic scale by roughly
the square-to-diamond fit factor while rotating. Compensating that scale removed
the prominent camera bounce at top-down, angled, and close views. A widened
native-yaw hysteresis window also substantially reduced disappearing building
sides at quarter-turn handoffs. Small rendering artifacts remain possible at
non-native camera angles and are intentionally deferred for later refinement.
