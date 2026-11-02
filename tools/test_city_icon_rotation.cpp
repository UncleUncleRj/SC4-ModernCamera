// Regression test for the rotation that keeps city icons looking the way the Classic camera
// shows them. The formulas it checks against come from the 641 executable, and the vertices
// come from the Zot_NoWork meshes in SimCity_1.dat. From a Visual Studio x86 command prompt
// at the repository root, in a scratch output folder of your choice:
//   cl /std:c++20 /EHsc /IDev\src tools\test_city_icon_rotation.cpp
#include "SC4CityIconRotation.h"

#include <cstdio>
#include <vector>

namespace
{
	struct Vector
	{
		float x;
		float y;
		float z;
	};

	struct View
	{
		float yaw;
		float pitch;
	};

	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kNativeYaw = -0.39269909f;
	constexpr float kNativePitch[5] = { 0.52359879f, 0.61086524f, 0.69813170f, 0.78539816f, 0.78539816f };
	// Extremes of the Modern pitch range and the native yaw window, with points between them.
	constexpr View kViews[] = {
		{ -1.61f, 0.261f }, { -1.61f, 1.483f }, { -1.0f, 0.7f }, { -0.39f, 1.2f },
		{ -0.2f, 0.261f }, { 0.13f, 0.9f }, { 0.13f, 1.483f },
	};
	// Zoom 0: the vertical card (S3D instance 1C440000).
	const std::vector<Vector> kCardMesh = {
		{ 9.210f, 24.438f, 5.638f }, { -5.171f, 24.438f, 11.595f }, { -5.171f, 0.472f, 11.595f }, { 9.210f, 0.472f, 5.638f },
	};
	// Zoom 3: the top and front corners of the textured box (S3D instance 1C440300).
	const std::vector<Vector> kBoxMesh = {
		{ -6.683f, 12.847f, 6.677f }, { 6.703f, 12.847f, 6.677f }, { 6.703f, 12.847f, -6.724f },
		{ -6.683f, 12.847f, -6.724f }, { -6.683f, -0.004f, 6.677f }, { 6.703f, -0.004f, 6.677f },
	};

	int failures = 0;

	void Check(bool passed, const char* name)
	{
		if (!passed) {
			std::printf("FAIL: %s\n", name);
			++failures;
		}
	}

	bool IsNear(float first, float second, float tolerance)
	{
		return std::abs(first - second) <= tolerance;
	}

	Vector Multiply(const float matrix[3][3], const Vector& v)
	{
		return {
			matrix[0][0] * v.x + matrix[0][1] * v.y + matrix[0][2] * v.z,
			matrix[1][0] * v.x + matrix[1][1] * v.y + matrix[1][2] * v.z,
			matrix[2][0] * v.x + matrix[2][1] * v.y + matrix[2][2] * v.z,
		};
	}

	// World to camera, as the native camera's inverse does it (0x7FF3D0).
	Vector ToScreen(const SC4CameraAxes& axes, const Vector& world)
	{
		return {
			axes.right[0] * world.x + axes.right[1] * world.y + axes.right[2] * world.z,
			axes.up[0] * world.x + axes.up[1] * world.y + axes.up[2] * world.z,
			axes.back[0] * world.x + axes.back[1] * world.y + axes.back[2] * world.z,
		};
	}

	// The rotation the model maker gives an icon at a quarter turn (0x48FA40).
	void GetNativeModelRotation(int32_t rotation, float result[3][3])
	{
		const float angle = static_cast<float>(-rotation) * kPi * 0.5f;
		const float matrix[3][3] = {
			{ std::cos(angle), 0.0f, std::sin(angle) },
			{ 0.0f, 1.0f, 0.0f },
			{ -std::sin(angle), 0.0f, std::cos(angle) },
		};
		for (int row = 0; row < 3; ++row) {
			for (int column = 0; column < 3; ++column) {
				result[row][column] = matrix[row][column];
			}
		}
	}

	// At Classic angles nothing may change: the result is the game's own transform.
	void TestClassicAnglesKeepNativeTransform()
	{
		for (int32_t zoom = 0; zoom < 5; ++zoom) {
			for (int32_t rotation = 0; rotation < 4; ++rotation) {
				float computed[3][3];
				float native[3][3];
				ComputeCityIconRotation(rotation, kNativeYaw, kNativePitch[zoom], kNativeYaw, kNativePitch[zoom], computed);
				GetNativeModelRotation(rotation, native);
				Check(IsSameCityIconRotation(computed, native), "Classic angles keep the native transform");
			}
		}
	}

	// The axes must match the native camera's own construction: offset from 0x7CBE40, then
	// look-at from 0x7FFD60.
	void TestAxesMatchNativeLookAt()
	{
		for (int32_t rotation = 0; rotation < 4; ++rotation) {
			for (const View& view : kViews) {
				const float azimuth = (static_cast<float>(rotation) + 1.0f) * kPi * 0.5f + view.yaw;
				const Vector offset = { std::cos(azimuth), std::tan(view.pitch), std::sin(azimuth) };
				const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
				const Vector back = { offset.x / length, offset.y / length, offset.z / length };
				const float rightLength = std::sqrt(back.x * back.x + back.z * back.z);
				const Vector right = { back.z / rightLength, 0.0f, -back.x / rightLength };
				const Vector up = {
					back.y * right.z - back.z * right.y,
					back.z * right.x - back.x * right.z,
					back.x * right.y - back.y * right.x,
				};

				const SC4CameraAxes axes = GetCameraAxes(rotation, view.yaw, view.pitch);
				const float kTolerance = 0.0001f;
				Check(IsNear(axes.back[0], back.x, kTolerance) && IsNear(axes.back[1], back.y, kTolerance)
					&& IsNear(axes.back[2], back.z, kTolerance), "back axis matches the native look-at");
				Check(IsNear(axes.right[0], right.x, kTolerance) && IsNear(axes.right[1], right.y, kTolerance)
					&& IsNear(axes.right[2], right.z, kTolerance), "right axis matches the native look-at");
				Check(IsNear(axes.up[0], up.x, kTolerance) && IsNear(axes.up[1], up.y, kTolerance)
					&& IsNear(axes.up[2], up.z, kTolerance), "up axis matches the native look-at");
			}
		}
	}

	// The mesh must land on the same screen coordinates as under the Classic camera, at every
	// view, and it must not without the rotation. The second half proves the fixture sees the bug.
	void TestIconDrawsAsClassicAtEveryView(const std::vector<Vector>& mesh, int32_t zoom)
	{
		bool nativeTransformDistorts = false;

		for (int32_t rotation = 0; rotation < 4; ++rotation) {
			const SC4CameraAxes classicAxes = GetCameraAxes(rotation, kNativeYaw, kNativePitch[zoom]);
			float classicModel[3][3];
			GetNativeModelRotation(rotation, classicModel);

			for (const View& view : kViews) {
				const SC4CameraAxes axes = GetCameraAxes(rotation, view.yaw, view.pitch);
				float model[3][3];
				ComputeCityIconRotation(rotation, view.yaw, view.pitch, kNativeYaw, kNativePitch[zoom], model);

				for (const Vector& vertex : mesh) {
					const Vector expected = ToScreen(classicAxes, Multiply(classicModel, vertex));
					const Vector actual = ToScreen(axes, Multiply(model, vertex));
					const Vector unrotated = ToScreen(axes, Multiply(classicModel, vertex));

					Check(IsNear(actual.x, expected.x, 0.001f) && IsNear(actual.y, expected.y, 0.001f)
						&& IsNear(actual.z, expected.z, 0.001f), "icon draws as under the Classic camera");
					nativeTransformDistorts = nativeTransformDistorts
						|| !IsNear(unrotated.x, expected.x, 1.0f) || !IsNear(unrotated.y, expected.y, 1.0f);
				}
			}
		}

		Check(nativeTransformDistorts, "the native transform distorts the icon at these views");
	}

	// A rigid rotation keeps the icon's size and cannot mirror it.
	void TestRotationIsRigid()
	{
		for (const View& view : kViews) {
			float model[3][3];
			ComputeCityIconRotation(2, view.yaw, view.pitch, kNativeYaw, kNativePitch[3], model);

			for (int first = 0; first < 3; ++first) {
				for (int second = 0; second < 3; ++second) {
					const float dot = model[first][0] * model[second][0]
						+ model[first][1] * model[second][1]
						+ model[first][2] * model[second][2];
					Check(IsNear(dot, first == second ? 1.0f : 0.0f, 0.0001f), "rotation is orthonormal");
				}
			}

			const float determinant =
				model[0][0] * (model[1][1] * model[2][2] - model[1][2] * model[2][1])
				- model[0][1] * (model[1][0] * model[2][2] - model[1][2] * model[2][0])
				+ model[0][2] * (model[1][0] * model[2][1] - model[1][1] * model[2][0]);
			Check(IsNear(determinant, 1.0f, 0.0001f), "rotation does not mirror");
		}
	}

	void TestSameRotationTolerance()
	{
		const float first[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		const float near[3][3] = { { 1.00005f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		const float far[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.001f }, { 0.0f, 0.0f, 1.0f } };
		Check(IsSameCityIconRotation(first, near), "small differences count as the same rotation");
		Check(!IsSameCityIconRotation(first, far), "larger differences count as a new rotation");
	}
}

int main()
{
	TestClassicAnglesKeepNativeTransform();
	TestAxesMatchNativeLookAt();
	TestIconDrawsAsClassicAtEveryView(kCardMesh, 0);
	TestIconDrawsAsClassicAtEveryView(kBoxMesh, 3);
	TestRotationIsRigid();
	TestSameRotationTolerance();

	if (failures != 0) {
		std::printf("%d checks failed\n", failures);
		return 1;
	}
	std::printf("All city icon rotation checks passed\n");
	return 0;
}
