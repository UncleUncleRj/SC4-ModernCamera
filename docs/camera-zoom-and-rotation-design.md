# Camera Zoom and Rotation Design

This document defines SC4-ModernCamera zoom, pitch, yaw, rotation, and WASD behavior.

## Core Principle

Zoom distance, pitch, and yaw are independent persistent camera state.

The mouse wheel changes zoom distance only. It must not select, restore, or
otherwise interfere with the player's pitch or yaw. Middle-mouse drag changes pitch
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

### Alt hold

- Works only in Modern camera mode.
- Requires city-view keyboard focus and is not captured while Ctrl or either
  Windows key is held.
- Pressing Alt saves the Modern rotation, yaw, pitch, zoom stage, and magnification, then
  moves to the nearest native Classic view: the nearest quarter turn, the Classic zoom
  level closest in on-screen size, and that level's native pitch. Classic zoom levels are
  stages 0 to 4 at magnification 1, plus stage 4 at magnification 2.
- Releasing Alt moves back to the exact saved view, whatever was done during the hold.
  With On Alt Release set to Keep View (`altCameraBungie` is `false`), the camera stays on the view
  it reached instead. A move still under way finishes, then the camera dips about 6 percent
  closer and settles back on that view, so the release has visible feedback.
- Each move takes 0.3 seconds, starts fast, passes its target slightly, and settles back.
  A press or release during a move continues from where the camera is, and the saved view
  stays the one from the first press. Free rotation and zoom input is ignored until a
  return to Modern arrives.
- Keeps the focus on the same point of the map throughout. A move only changes the view,
  so it does not run the native target clamp. Panning during the hold is kept on release.
- While held, the view stays on Classic angles and zoom levels, so it is not free rotation.
  Q and E, and each notch of the wheel, make one animated step. An M3 horizontal drag
  takes the nearest quarter turn to how far the mouse has moved, and M3 vertical movement
  is ignored.
- Focus loss, a mode change, and a camera reset return to the saved view at once under
  either setting, because frames may not run afterwards and Alt+Tab should not change the view. A save or bookmark first completes any move in progress.
- Leaves the Alt key visible to Windows, so Alt+Tab, Alt+F4, and other Alt chords
  keep working. The canvas filter consumes the Alt messages themselves so a plain
  press does not open the system menu.

### WASD movement

- Optional in Modern camera mode.
- Moves the camera forward, back, left, and right while W/A/S/D is held.
- Uses a frame-driven held-key state, not operating-system key repeat, so movement stops immediately on key release.
- Shift applies a movement-speed boost.
- The panning speed setting applies to WASD movement, not right-mouse panning.
- Keeps a constant on-screen speed while zooming. Native pan speed follows the zoom
  stage alone, so the speed is divided by magnification. The only remaining step is
  the native one between stages 2 and 3, about 1.14 times.
- Uses the same playable-map boundary rules as the other Modern camera movement paths.
- Does not engage during right-click scrolling.
- Remains active during M3 camera maneuvering and zoom.
- Captures W/A/S/D only while the city view has keyboard focus and Ctrl, Alt,
  and the Windows keys are up. When one of those shortcut modifiers is held,
  Modern Camera stops any held movement and leaves the key for SC4.

## Rotation Pivot and Anchor

At the beginning of an M3 rotation gesture, capture SC4's valid ground
focus and freeze it until M3 is released. Pitch and yaw rotate the camera around
that pivot. This is the conventional stable orbit-camera model: the camera
position traces the arc while the ground anchor does not drift.

The initial implementation freezes both known target fields:

- `viewTargetPosition`
- `baseTargetForRotation`

It also suppresses target velocity during the gesture. This should prevent
inertial focus drift and give SC4 a stable logical point on playable terrain for
rendering and culling.

Each rotation step restores the frozen target exactly, so a target that differs at
the start of the next step was moved on purpose by held-key or right-drag panning.
The pivot follows that movement, which is what keeps WASD active during a gesture.

The physical camera should remain within the playable-map constraints
enforced by SC4. Rotation near map boundaries requires runtime testing. If an
orbit would place the camera outside valid bounds, the implementation may clamp the
orbit radius or blend toward in-place rotation.

### Possible dual-anchor extension

SC4 may distinguish the visible lens target from the logical target used for
culling. If runtime validation confirms that distinction, the implementation may use:

- A moving visual target derived from the requested pitch/yaw.
- A frozen or map-clamped logical ground anchor used by SC4's renderer.

For a stationary camera, the visible ground target for yaw would trace a circle
around the camera's ground projection. Its approximate radius on level terrain
would be `camera height / tan(downward pitch)`. This is not enabled until the
roles of the reconstructed camera fields are validated; moving the wrong target
could reintroduce drift, disappearing buildings, or void-rendering artifacts.

## Rendering and Culling

SC4 can hide buildings or produce artifacts when its camera/focus state points
toward the surrounding void. A stable valid ground pivot is the conservative
first defense against that behavior.

Anchor stability cannot be assumed to solve every culling problem. Native zoom
stage, renderer refresh, camera position, and an internal logical focus may all
participate. Ctrl+Alt+F8 camera dumps and gesture/zoom logging should be used to compare
healthy and broken states before assigning unverified meanings to fields.

## Implementation Behavior

- Wheel zoom uses continuous magnification across SC4's native zoom stages.
- Wheel zoom reapplies the player's current pitch and yaw after native stage
  changes.
- M3 horizontal drag changes yaw.
- M3 vertical drag changes pitch.
- M3 down freezes the rotation pivot; M3 up releases it.
- M3 captures the orthographic scale. During rotation, custom
  magnification counters SC4's square-to-diamond bounds-fit so the visible zoom
  does not bounce merely to keep every map corner inside the viewport. The
  magnification is computed before the refresh from the native formula,
  `orthoScale = fit(yaw) * K(zoom) / magnification` with
  `fit(yaw) = cos|yaw| + sin|yaw|`, so one refresh per step is enough. Wheel zoom
  during a gesture updates the scale being held.
- An Alt-hold move interpolates the view's direction, pitch, and on-screen zoom once per
  camera frame. Yaw is rebalanced into the native yaw window on the way, as M3 rotation
  does, which leaves the view unchanged but keeps the native fit factor usable: it falls
  quickly past -90 degrees and inverts the image past -135. Magnification follows the fit
  factor and the native step between stages 2 and 3, so the size changes evenly. The
  zoom stage changes once, at the middle of the distance. The last frame writes the
  target's exact values, so a return to Modern ends on the saved view.
- Optional WASD movement consumes game W/A/S/D input only while Modern camera mode
  and WASD Movement are enabled, the city view has keyboard focus, and no Ctrl,
  Alt, or Windows-key shortcut modifier is held. Movement is smooth while held,
  stops on key release, supports Shift boost, uses the panning speed setting,
  and respects the same map bounds as other camera movement. A shortcut modifier
  stops any held Modern Camera movement and returns the movement key to SC4.
- SC4's standard arrow-key panning remains available independently of the WASD
  Movement setting. In Modern camera mode, WASD movement and arrow-key panning
  share the same internal `SetScrolling` path so focus gating, panning speed,
  Shift boost, modifier pass-through, and map-boundary handling stay consistent.
- Native yaw uses a `[-70 degrees, +30 degrees]` hysteresis window. Building
  side meshes were observed disappearing when an exact 90-degree window mapped
  a healthy low-end orientation directly onto the opposite high-end boundary.
  The wider window delays that handoff and maps it into the safer interior of
  the adjacent bucket instead.
- Gesture anchor capture/release and every zoom update are logged at Verbose.
- M3 mouse movement accumulates and is applied once per camera frame. The hidden
  cursor is recentered during the gesture so rotation cannot stop at a screen edge,
  and it returns to where the gesture began on release.
- Automatic pitch curves, vertical-M3 dolly zoom, and Shift free-look mode are
  outside this design.

## Camera Updates

Native evidence for this section is in [default-camera.md](default-camera.md).

Continuous steps (M3, Q/E, wheel) recompute the camera silently, using update mode 1
followed by the native target clamp. They do not use the broadcast update (mode 2)
because its zoom and rotation messages describe discrete changes: repeating them
re-runs work such as the render-grid rebuild and the model maker's pass over occupants.

A camera frame runs once per framework tick, after message delivery and before the
draw system. It completes what the steps requested, so any number of input messages
costs at most one of each:

| Change | Render grid | Notification |
| --- | --- | --- |
| M3 or Q/E step | Rebuilt | `CameraUpdated` only |
| Wheel zoom | Not rebuilt, because the grid depends on orientation and not scale | `CameraUpdated` only |
| Alt-hold move in progress | Rebuilt when the orientation changed | `CameraUpdated` only |
| Gesture end, end of an Alt-hold move, bookmark, save normalization, mode change | Rebuilt | One broadcast update |

The render grid must be rebuilt every frame the orientation changes. Models are
placed using the current yaw and pitch while the draw query uses the current camera,
so a stale grid hides models that are on screen, and the error grows with zoom.
Throttling the rebuild would therefore cause visible popping at close zoom.

The broadcast update first sets the previous and pending zoom and rotation fields to
the current values, so subscribers see no discrete change. It runs when a gesture
ends so that view-dependent systems such as automata re-evaluate once.

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

## Runtime Characteristics

Runtime testing confirmed that SC4 expands its orthographic scale by roughly
the square-to-diamond fit factor while rotating. Compensating that scale removed
the prominent camera bounce at top-down, angled, and close views. A widened
native-yaw hysteresis window also substantially reduced disappearing building
sides at quarter-turn handoffs. Small rendering artifacts remain possible at
non-native camera angles.
