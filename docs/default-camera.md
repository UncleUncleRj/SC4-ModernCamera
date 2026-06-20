# SimCity 4 default camera behavior

This document records the default SimCity 4 camera behavior.

The measurements were collected in Small, medium, and large
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
player exits one city and loads another. A newly loaded city can therefore begin
with angles inherited from the previous city.

Code that needs the native defaults must preserve or reconstruct the original
tables independently of the camera state loaded from a city. Capturing the active
camera immediately after city load is not sufficient once the global tables have
already been modified.

For game version 641, the currently used addresses are documented in
`Dev/src/SC4CameraController.cpp` as `kPitchAddress1`, `kPitchAddress2`,
`kYawAddress0`, `kYawAddress1`, and `kYawAddress2`.

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
