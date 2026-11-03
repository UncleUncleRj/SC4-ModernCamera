#pragma once

#include "SC4BakedComposite.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// How one part of a building shape is built.
enum class SC4ShapePartKind
{
	// A solid block: four walls and a flat top.
	Box,
	// A roof whose sides slope in from its lower edge to a smaller flat top, which may shrink to a
	// ridge or a point.
	HipRoof,
	// A flat area with no walls, such as an apron, a yard, or a parking area.
	Ground,
};

// One part of a building's true shape, in the model space of rotation variant 0.
struct SC4ShapePart
{
	// How the part is built.
	SC4ShapePartKind kind = SC4ShapePartKind::Box;
	// Lowest x, y, and z of the part. A ground part lies at this y.
	float minimum[3] = {};
	// Highest x, y, and z of the part.
	float maximum[3] = {};
	// Lowest x and z of a hip roof's top.
	float topMinimum[2] = {};
	// Highest x and z of a hip roof's top.
	float topMaximum[2] = {};
};

// The true shape of one building. Its pictures are painted onto this shape in place of the
// low-detail shape the game ships, which only has to look right from the native angles.
struct SC4BuildingShape
{
	// Name used in logs.
	std::string name;
	// Group of the building's model resources.
	uint32_t group = 0;
	// Instance of the building's model resources, with the zoom and rotation digits zero.
	uint32_t baseInstance = 0;
	// Parts of the shape, which may overlap.
	std::vector<SC4ShapePart> parts;
};

namespace SC4BuildingShapeDetail
{
	// A corner of a shape face, in the model space of rotation variant 0.
	using Corner = std::array<float, 3>;

	/// <summary>
	/// Appends a flat face with four corners in order around it, wound so that its front faces
	/// outward. A corner may repeat, which leaves a triangle, and a face with no area is skipped.
	/// </summary>
	/// <param name="mesh">Mesh to extend, in the variant's model space.</param>
	/// <param name="corners">The face's corners in variant 0's model space.</param>
	/// <param name="outward">Any direction on the front side of the face.</param>
	/// <param name="variant">Rotation variant whose model space the mesh uses.</param>
	inline void AddFace(SC4BakedMesh& mesh, std::array<Corner, 4> corners, const Corner& outward, int32_t variant)
	{
		// Newell's method gives the face's normal even when a corner repeats.
		float normal[3] = {};
		for (size_t i = 0; i < 4; ++i) {
			const Corner& p = corners[i];
			const Corner& q = corners[(i + 1) % 4];
			normal[0] += (p[1] - q[1]) * (p[2] + q[2]);
			normal[1] += (p[2] - q[2]) * (p[0] + q[0]);
			normal[2] += (p[0] - q[0]) * (p[1] + q[1]);
		}
		if (normal[0] * outward[0] + normal[1] * outward[1] + normal[2] * outward[2] < 0.0f) {
			std::swap(corners[1], corners[3]);
		}

		static constexpr int32_t kTriangles[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };
		for (const auto& triangle : kTriangles) {
			const Corner& a = corners[static_cast<size_t>(triangle[0])];
			const Corner& b = corners[static_cast<size_t>(triangle[1])];
			const Corner& c = corners[static_cast<size_t>(triangle[2])];
			const float ex = b[0] - a[0], ey = b[1] - a[1], ez = b[2] - a[2];
			const float fx = c[0] - a[0], fy = c[1] - a[1], fz = c[2] - a[2];
			const float nx = ey * fz - ez * fy;
			const float ny = ez * fx - ex * fz;
			const float nz = ex * fy - ey * fx;
			if (nx * nx + ny * ny + nz * nz < 1e-8f || mesh.vertices.size() + 3 > 65535) {
				continue;
			}
			for (const int32_t corner : triangle) {
				SC4BakedVertex vertex{};
				TurnQuarter(corners[static_cast<size_t>(corner)].data(), -variant, vertex.position);
				mesh.indices.push_back(static_cast<uint16_t>(mesh.vertices.size()));
				mesh.vertices.push_back(vertex);
			}
		}
	}
}

/// <summary>
/// Builds a shape's faces in the model space of one rotation variant, as a model with one mesh and
/// no texture coordinates. Faces point outward. No part has a bottom face, since no bake camera
/// sees one, and faces hidden inside other parts are left for clipping to remove.
/// </summary>
/// <param name="shape">Shape to build.</param>
/// <param name="variant">Rotation variant whose model space the model uses.</param>
/// <param name="model">Receives the model.</param>
inline void BuildShapeModel(const SC4BuildingShape& shape, int32_t variant, SC4BakedModel& model)
{
	using namespace SC4BuildingShapeDetail;

	static constexpr Corner kUp = { 0.0f, 1.0f, 0.0f };
	static constexpr Corner kPositiveX = { 1.0f, 0.0f, 0.0f };
	static constexpr Corner kNegativeX = { -1.0f, 0.0f, 0.0f };
	static constexpr Corner kPositiveZ = { 0.0f, 0.0f, 1.0f };
	static constexpr Corner kNegativeZ = { 0.0f, 0.0f, -1.0f };

	model.assign(1, SC4BakedMesh{});
	SC4BakedMesh& mesh = model.front();
	for (const SC4ShapePart& part : shape.parts) {
		const float x0 = part.minimum[0];
		const float y0 = part.minimum[1];
		const float z0 = part.minimum[2];
		const float x1 = part.maximum[0];
		const float y1 = part.maximum[1];
		const float z1 = part.maximum[2];
		if (part.kind == SC4ShapePartKind::Ground) {
			AddFace(mesh, { { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } } }, kUp, variant);
			continue;
		}

		const bool hip = part.kind == SC4ShapePartKind::HipRoof;
		const float topX0 = hip ? part.topMinimum[0] : x0;
		const float topZ0 = hip ? part.topMinimum[1] : z0;
		const float topX1 = hip ? part.topMaximum[0] : x1;
		const float topZ1 = hip ? part.topMaximum[1] : z1;
		const Corner bottom00 = { x0, y0, z0 };
		const Corner bottom10 = { x1, y0, z0 };
		const Corner bottom11 = { x1, y0, z1 };
		const Corner bottom01 = { x0, y0, z1 };
		const Corner top00 = { topX0, y1, topZ0 };
		const Corner top10 = { topX1, y1, topZ0 };
		const Corner top11 = { topX1, y1, topZ1 };
		const Corner top01 = { topX0, y1, topZ1 };
		AddFace(mesh, { bottom00, bottom10, top10, top00 }, kNegativeZ, variant);
		AddFace(mesh, { bottom10, bottom11, top11, top10 }, kPositiveX, variant);
		AddFace(mesh, { bottom11, bottom01, top01, top11 }, kPositiveZ, variant);
		AddFace(mesh, { bottom01, bottom00, top00, top01 }, kNegativeX, variant);
		AddFace(mesh, { top00, top01, top11, top10 }, kUp, variant);
	}
}

/// <summary>
/// Paints a building's true shape with one rotation variant's picture. Each point of the shape
/// takes the pixel the bake camera saw in its direction, so the picture lines up exactly at the
/// bake view and lands on the right surfaces from any other view. Parts of the shape outside the
/// picture are left out, since this variant has nothing to show there.
/// </summary>
/// <param name="picture">The variant's meshes, clipped to what the bake camera saw. Together they cover the picture.</param>
/// <param name="mappings">How each of those meshes' textures lies over the bake view, by mesh.</param>
/// <param name="shape">The shape in the variant's model space, clipped to what the bake camera saw.</param>
/// <param name="bake">Axes of the bake camera.</param>
/// <param name="result">Receives one mesh per picture mesh, holding the parts of the shape its texture covers.</param>
inline void PaintShape(
	const SC4BakedModel& picture,
	const std::vector<SC4BakeMapping>& mappings,
	const SC4BakedModel& shape,
	const SC4CameraAxes& bake,
	SC4BakedModel& result)
{
	using namespace SC4BakedCompositeDetail;

	// Share of a shape triangle one texture must cover for the triangle to stay whole.
	constexpr float kWholeCoverage = 0.999f;

	// A triangle of the picture as seen from the bake camera.
	struct Tile
	{
		// Picture mesh holding the triangle.
		uint32_t mesh;
		// Corners in counterclockwise order.
		Point ordered[3];
		// Bounds.
		float minX;
		float maxX;
		float minY;
		float maxY;
	};

	const auto see = [&](const float* position) {
		return Point{
			position[0] * bake.right[0] + position[1] * bake.right[1] + position[2] * bake.right[2],
			position[0] * bake.up[0] + position[1] * bake.up[1] + position[2] * bake.up[2] };
	};
	const auto twiceSignedArea = [](const Point corner[3]) {
		return (corner[1].x - corner[0].x) * (corner[2].y - corner[0].y) - (corner[2].x - corner[0].x) * (corner[1].y - corner[0].y);
	};

	std::vector<Tile> tiles;
	for (uint32_t m = 0; m < picture.size() && m < mappings.size(); ++m) {
		if (!mappings[m].valid) {
			continue;
		}
		const SC4BakedMesh& mesh = picture[m];
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			Point corner[3];
			bool inRange = true;
			for (size_t c = 0; c < 3 && inRange; ++c) {
				const uint16_t index = mesh.indices[i + c];
				inRange = index < mesh.vertices.size();
				if (inRange) {
					corner[c] = see(mesh.vertices[index].position);
				}
			}
			const float signedArea = inRange ? twiceSignedArea(corner) : 0.0f;
			if (std::abs(signedArea) < 1e-6f) {
				continue;
			}
			Tile tile{};
			tile.mesh = m;
			tile.ordered[0] = corner[0];
			tile.ordered[1] = signedArea > 0.0f ? corner[1] : corner[2];
			tile.ordered[2] = signedArea > 0.0f ? corner[2] : corner[1];
			tile.minX = (std::min)({ corner[0].x, corner[1].x, corner[2].x });
			tile.maxX = (std::max)({ corner[0].x, corner[1].x, corner[2].x });
			tile.minY = (std::min)({ corner[0].y, corner[1].y, corner[2].y });
			tile.maxY = (std::max)({ corner[0].y, corner[1].y, corner[2].y });
			tiles.push_back(tile);
		}
	}

	result.assign(picture.size(), SC4BakedMesh{});
	std::vector<std::pair<uint32_t, Fragment>> pieces;
	std::vector<float> covered(picture.size());
	for (const SC4BakedMesh& mesh : shape) {
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			const SC4BakedVertex* vertex[3] = {};
			Point corner[3];
			bool inRange = true;
			for (size_t c = 0; c < 3 && inRange; ++c) {
				const uint16_t index = mesh.indices[i + c];
				inRange = index < mesh.vertices.size();
				if (inRange) {
					vertex[c] = &mesh.vertices[index];
					corner[c] = see(vertex[c]->position);
				}
			}
			const float signedArea = inRange ? twiceSignedArea(corner) : 0.0f;
			if (std::abs(signedArea) < 1e-6f) {
				continue;
			}

			Fragment whole;
			whole.Add(corner[0]);
			whole.Add(signedArea > 0.0f ? corner[1] : corner[2]);
			whole.Add(signedArea > 0.0f ? corner[2] : corner[1]);
			const float minX = (std::min)({ corner[0].x, corner[1].x, corner[2].x });
			const float maxX = (std::max)({ corner[0].x, corner[1].x, corner[2].x });
			const float minY = (std::min)({ corner[0].y, corner[1].y, corner[2].y });
			const float maxY = (std::max)({ corner[0].y, corner[1].y, corner[2].y });

			pieces.clear();
			std::fill(covered.begin(), covered.end(), 0.0f);
			for (const Tile& tile : tiles) {
				if (tile.minX >= maxX || tile.maxX <= minX || tile.minY >= maxY || tile.maxY <= minY) {
					continue;
				}
				Fragment rest = whole;
				bool usable = true;
				for (int32_t edge = 0; edge < 3 && rest.count >= 3; ++edge) {
					Fragment inside;
					Fragment outside;
					if (!Split(rest, tile.ordered[edge], tile.ordered[(edge + 1) % 3], inside, outside)) {
						usable = false;
						break;
					}
					rest = inside;
				}
				const float area = usable && rest.count >= 3 ? GetArea(rest) : 0.0f;
				if (area > kBakedMinimumFragmentArea) {
					pieces.emplace_back(tile.mesh, rest);
					covered[tile.mesh] += area;
				}
			}

			const auto emit = [&](uint32_t target, const Fragment& fragment) {
				SC4BakedMesh& painted = result[target];
				const SC4BakeMapping& mapping = mappings[target];
				const size_t start = painted.vertices.size();
				if (start + static_cast<size_t>(fragment.count) > 65535) {
					return;
				}
				for (int32_t c = 0; c < fragment.count; ++c) {
					const Point& point = fragment.points[static_cast<size_t>(c)];
					SC4BakedVertex paintedVertex = InterpolateVertex(point, corner, *vertex[0], *vertex[1], *vertex[2]);
					paintedVertex.uv[0] = static_cast<float>(mapping.u[0] * point.x + mapping.u[1] * point.y + mapping.u[2]);
					paintedVertex.uv[1] = static_cast<float>(mapping.v[0] * point.x + mapping.v[1] * point.y + mapping.v[2]);
					painted.vertices.push_back(paintedVertex);
				}
				for (int32_t c = 1; c + 1 < fragment.count; ++c) {
					const uint16_t first = static_cast<uint16_t>(start);
					const uint16_t second = static_cast<uint16_t>(start + static_cast<size_t>(c));
					const uint16_t third = static_cast<uint16_t>(start + static_cast<size_t>(c) + 1);
					painted.indices.push_back(first);
					painted.indices.push_back(signedArea > 0.0f ? second : third);
					painted.indices.push_back(signedArea > 0.0f ? third : second);
				}
			};

			// A triangle inside one texture keeps its corners. One that crosses into another texture,
			// or past the edge of the picture, is cut where the picture changes.
			const float wholeArea = GetArea(whole);
			bool emitted = false;
			for (uint32_t m = 0; m < covered.size() && !emitted; ++m) {
				if (covered[m] >= wholeArea * kWholeCoverage) {
					emit(m, whole);
					emitted = true;
				}
			}
			if (!emitted) {
				for (const auto& [target, piece] : pieces) {
					emit(target, piece);
				}
			}
		}
	}
}
