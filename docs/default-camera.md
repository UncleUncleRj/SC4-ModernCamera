# SimCity 4 default camera behavior

This document records the default SimCity 4 camera behavior.

The measurements were collected in small, medium, and large
blank city tiles on a 1920 x 1080 viewport.

## Native zoom states

The native camera uses five discrete zoom stages. Pitch is selected from a
process-global table; yaw remains fixed at -22.5 degrees.

| Zoom | Pitch (degrees) | Pitch (radians) | Yaw (degrees) | Ortho scale at 1920 x 1080 |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 30 | 0.523599 | -22.5 | 2.613126 |
| 1 | 35 | 0.610865 | -22.5 | 1.306563 |
| 2 | 40 | 0.698132 | -22.5 | 0.653282 |
| 3 | 45 | 0.785398 | -22.5 | 0.286370 |
| 4 | 45 | 0.785398 | -22.5 | 0.143185 |

At the additional closest native zoom step, the discrete zoom remains 4,
`customMagnification` becomes 2.0, and the observed orthographic scale becomes
0.071592. Orthographic scale should be treated as viewport- and magnification-
dependent rather than as a universal constant.

The two native pitch tables both contain:

```text
[0.523599, 0.610865, 0.698132, 0.785398, 0.785398]
```

The yaw instruction value and both native yaw tables contain `-0.392699`
(-22.5 degrees) for every zoom stage.

## Native rotation

The native rotation buttons change the discrete `rotation` field through values
0, 1, 2, and 3. They do not change the camera-control `yaw`, which remains at
-22.5 degrees.

Quarter-turn rotation is represented by changes to derived state including:

- `cameraOffset`
- `baseTargetForRotation`
- `groundRayDirection`
- `cachedViewXformA` and `cachedViewXformB`

Consequently, the discrete rotation should be preserved and applied through the
game's native rotation path. It should not be reconstructed by adding quarter
turns to the `yaw` field.

## City tile size

The camera control reports the following world dimensions:

| Tile size | `citySizeX` | `citySizeZ` |
| --- | ---: | ---: |
| Small | 1024 | 1024 |
| Medium | 2048 | 2048 |
| Large | 4096 | 4096 |

The native pitch table, yaw, and same-zoom orthographic scales were identical for
all three tile sizes. Tile size affects target placement and framing, but does not
require a different native orientation table.

## Global table lifetime

The pitch and yaw tables are process-global, not city-local. If a plugin replaces
every entry with custom free-camera angles, those values remain active when the
player exits one city and loads another. A loaded city can therefore begin
with angles inherited from the previous city.

Code that needs the native defaults must preserve or reconstruct the original
tables independently of the camera state loaded from a city. Capturing the active
camera immediately after city load is not sufficient once the global tables have
already been modified.

For game version 641, the addresses the plugin patches are documented in
`Dev/src/SC4CameraController.cpp` as `kPitchAddress1`, `kPitchAddress2`, and
`kYawAddress1`.

Two other yaw values are deliberately left native. The yaw operand at `0x7CCB0A`
is an instruction in the camera-control constructor `0x7CC990`, and it only sets
a new control's starting yaw. The table at `0xABACB8` has no reference in the
`.text` section or the data-reference export.

## Native camera updates

These notes are binary-verified from the 641 executable. They have not been
observed at runtime except where the plugin's own behavior shows them.

`UpdateCameraPosition` (`0x7CCF80`) takes a mode:

| Mode | Recomputes camera | Scroll polygon | Messages | Clamps target |
| ---: | --- | --- | --- | --- |
| 0 | Yes | If empty | `PostViewInit` | No |
| 1 | Yes | Rebuilt | None | No |
| 2 | Yes | Rebuilt | `ZRChange1/2/3`, `CameraUpdated` | Yes (`0x7CD000`) |
| 3 | Without projection | If empty | `CameraUpdated`, `TChange2/3` | No |
| 4 | Yes | If empty | `PostViewRefresh` | No |
| 5 | Yes | If empty | None | No |

The previous and pending zoom and rotation fields (`+0x100`, `+0x110`) are message
payload. Native setters fill them just before broadcasting, and nothing clears
them. Native `SetCustomMagnification` (`0x7CD6E0`) sends only `PostViewRefresh`,
which requests a full redraw.

Many systems subscribe to the zoom and rotation messages, and several act on any
difference between the two payloads:

- The renderer rebuilds its render grid: `ZRChange2` runs `0x7CAB30` when the
  rotation differs, and `ZRChange3` runs `0x7CAA60` when the zoom differs.
- The model maker (`0x495F10`) can make a pass over occupants on `ZRChange2`
  (`0x495B00`). It returns early when both payloads match.
- Automata and traffic (`0x506970`, `0x4B0C10`) run their view update on every
  `ZRChange3`, whatever the payload says.

In the native handler map, only the renderer handles `CameraUpdated`. It
recalculates the view focus and visible cell range (`0x7C4910`), which the camera
position changes.

The target clamp (`0x7CD000`) clips the path from the base target, a city corner, to
the view target against the scroll polygon (`0x7CC650`). When it moves the target, it
places it on the `y = 0` plane (`0x7CBA90`) rather than on the terrain, so the target
ends up below the ground and the view swings when the camera later turns. If the clip
runs out of edges, the result is the base target itself. Native panning (`0x7CD330`)
clips from the current target instead and casts onto the terrain.

A static model of these routines finds that at normal pitch the polygon contains Small
and Large tiles, except at the exact corners of a Large tile. Low pitch combined with
the farthest zoom makes the clamp fire across the tile. This was observed at runtime on
a Small tile: at zoom stage 0, magnification 0.5, and pitch 0.435, a wheel zoom moved
the target from (566, 319, 646) to (101, 0, 52), and in the same state the clamp sent
the target to the base target at (0, 0, 0).

The camera position logged at city load was computed before the city size was set, so
it uses the constructor's default size of 4096 whatever the tile size is.

## Render grid

The renderer draws models found through a grid. A model's grid rectangle
(`0x7C1B30`) is projected with the cached sine and cosine of the current yaw and
pitch, plus fixed offsets per rotation at renderer `+0x268` and `+0x278`. The
projection does not use the view target, zoom stage, or magnification. The draw query
(`0x7C1FD0`) projects the viewport corners with the current camera. A grid built
for another orientation therefore misses models that are on screen.

`0x7CAB30` re-inserts every model (`0x7C5370`) using the rotation it is given and
requests a full redraw. It returns without working if its previous and new rotation
match, so a rebuild at the same rotation passes any other value as the previous one.

## Region-preview implications

The region preview can inherit a free-camera tilt when a city is saved while
custom angles are active. A preview-safe normalization should:

1. Preserve the current view target and discrete rotation.
2. Select native pitch from the table using the current discrete zoom.
3. Restore yaw to -22.5 degrees.
4. Let SC4 recalculate its derived camera transforms.
5. Capture the region preview.
6. Restore the player's free-camera state when the city remains open.

The normalization must occur at preview-capture time. Restoring values only after
the city begins shutting down may be too late, and restoring only the active
camera does not repair contaminated process-global angle tables.
