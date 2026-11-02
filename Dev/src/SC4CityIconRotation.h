#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

// Native model transform. Binary-verified on the 641 executable: 0x48FA40 fills it,
// cIS3DModelInstance slots 9 and 10 (0x801B80, 0x801BE0) copy it, and 0x7D3400 composes
// it with the view. A model vertex v draws at rotation * v * scale + translation.
class cS3DTransform
{
public:
	// Change counter that native code increments on each write.
	uint8_t version;
	// Parts in use, as a combination of the kS3DTransform flags below.
	uint8_t flags;
	// Padding retained for binary layout.
	uint8_t pad02[2];
	// Rotation, stored row by row.
	float rotation[3][3];
	// World position of the model's origin.
	float translation[3];
	// Uniform scale, used only when kS3DTransformHasScale is set.
	float scale;
};

static_assert(offsetof(cS3DTransform, flags) == 0x01);
static_assert(offsetof(cS3DTransform, rotation) == 0x04);
static_assert(offsetof(cS3DTransform, translation) == 0x28);
static_assert(offsetof(cS3DTransform, scale) == 0x34);
static_assert(sizeof(cS3DTransform) == 0x38);

// Transform flag marking a translation (binary-verified: 0x48FA40).
inline constexpr uint8_t kS3DTransformHasTranslation = 0x01;
// Transform flag marking a uniform scale (binary-verified: 0x7D3400).
inline constexpr uint8_t kS3DTransformHasScale = 0x02;
// Transform flag marking a rotation (binary-verified: 0x48FA40).
inline constexpr uint8_t kS3DTransformHasRotation = 0x04;

// Largest difference per matrix element treated as the same rotation. At the farthest icon
// vertex, about 30 world units from its origin, this stays well under a pixel at the closest zoom.
inline constexpr float kCityIconRotationTolerance = 0.0001f;

// Axes of a native camera in world space, which are the columns of its camera-to-world rotation.
struct SC4CameraAxes
{
	// Points to the right of the screen.
	float right[3];
	// Points to the top of the screen.
	float up[3];
	// Points from the view target toward the camera.
	float back[3];
};

/// <summary>
/// Returns the axes of a native camera. Binary-verified: 0x7CBE40 gives the azimuth and
/// camera offset, and 0x7FFD60 looks at the target with +Y as up.
/// </summary>
/// <param name="rotation">Native quarter-turn rotation.</param>
/// <param name="yaw">Yaw in radians.</param>
/// <param name="pitch">Pitch in radians.</param>
/// <returns>The camera's axes in world space.</returns>
inline SC4CameraAxes GetCameraAxes(int32_t rotation, float yaw, float pitch)
{
	const float azimuth = (static_cast<float>(rotation) + 1.0f) * 1.57079637f + yaw;
	const float sinAzimuth = std::sin(azimuth);
	const float cosAzimuth = std::cos(azimuth);
	const float sinPitch = std::sin(pitch);
	const float cosPitch = std::cos(pitch);

	return {
		{ sinAzimuth, 0.0f, -cosAzimuth },
		{ -sinPitch * cosAzimuth, cosPitch, -sinPitch * sinAzimuth },
		{ cosPitch * cosAzimuth, sinPitch, cosPitch * sinAzimuth },
	};
}

/// <summary>
/// Computes the model rotation that draws a city icon exactly as the Classic camera does.
/// Icon meshes are baked to look flat from one direction: that of the native camera at
/// rotation 0. Natively that camera always sees them from there, because the model maker
/// turns each model by -rotation quarter turns (0x48FA40). This rotation does the same for any view, as
/// camera-to-world * baked-world-to-camera, and it reduces to the native turn at Classic angles.
/// </summary>
/// <param name="rotation">Current native quarter-turn rotation.</param>
/// <param name="yaw">Current yaw in radians.</param>
/// <param name="pitch">Current pitch in radians.</param>
/// <param name="nativeYaw">Native yaw in radians.</param>
/// <param name="nativePitch">Native pitch of the current zoom stage in radians.</param>
/// <param name="result">Receives the rotation, row by row.</param>
inline void ComputeCityIconRotation(
	int32_t rotation,
	float yaw,
	float pitch,
	float nativeYaw,
	float nativePitch,
	float result[3][3])
{
	const SC4CameraAxes current = GetCameraAxes(rotation, yaw, pitch);
	const SC4CameraAxes baked = GetCameraAxes(0, nativeYaw, nativePitch);

	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			result[row][column] = current.right[row] * baked.right[column]
				+ current.up[row] * baked.up[column]
				+ current.back[row] * baked.back[column];
		}
	}
}

/// <summary>
/// Returns whether two rotations draw the same, so unchanged models are not written again.
/// </summary>
/// <param name="first">First rotation, row by row.</param>
/// <param name="second">Second rotation, row by row.</param>
/// <returns>True when every element is within kCityIconRotationTolerance.</returns>
inline bool IsSameCityIconRotation(const float first[3][3], const float second[3][3])
{
	for (int row = 0; row < 3; ++row) {
		for (int column = 0; column < 3; ++column) {
			if (std::abs(first[row][column] - second[row][column]) > kCityIconRotationTolerance) {
				return false;
			}
		}
	}
	return true;
}
