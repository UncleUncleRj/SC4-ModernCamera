#pragma once

#include "SC4CityIconRotation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

// One vertex of a baked mesh. Native vertex formats 0x80004001 and 0x2 both have this layout,
// and every full model in SimCity_1.dat uses one of the two.
struct SC4BakedVertex
{
	// Position in model space.
	float position[3];
	// Texture coordinate.
	float uv[2];
};

static_assert(sizeof(SC4BakedVertex) == 20);

// The triangles of one mesh of a baked model. All of them sample one texture.
struct SC4BakedMesh
{
	// Vertices used by the triangles.
	std::vector<SC4BakedVertex> vertices;
	// Three vertex indices per triangle.
	std::vector<uint16_t> indices;
};

// One rotation variant of a baked model. Each mesh is one texture.
using SC4BakedModel = std::vector<SC4BakedMesh>;

// One mesh of a composite model, ready to draw in the current variant's model space.
struct SC4CompositeMesh
{
	// Rotation variant the texture comes from.
	int32_t variant;
	// Mesh of that variant the texture comes from.
	uint32_t meshIndex;
	// Vertices used by the triangles.
	std::vector<SC4BakedVertex> vertices;
	// Three vertex indices per triangle.
	std::vector<uint16_t> indices;
};

// Largest difference in depth, in world units, treated as the same surface when clipping.
inline constexpr float kBakedDepthTolerance = 0.02f;
// Smallest fragment kept when clipping, in square world units as seen from the bake camera.
inline constexpr float kBakedMinimumFragmentArea = 0.0001f;
// Distance in world units each finished fragment grows past its edges, so neighbours overlap. Where
// clipping cut a surface, neighbours no longer share exact corners and rounding leaves pinholes along
// their edges. Fragments of one picture show the same pixels where they overlap, and between two
// pictures the strip is narrower than a pixel at the closest zoom.
inline constexpr float kCompositeSeamOverlap = 0.05f;
// Smallest dot product of two unit normals on the same plane.
inline constexpr float kCompositeSamePlaneNormal = 0.9995f;
// Largest distance in world units between two surfaces on the same plane.
inline constexpr float kCompositeSamePlaneDistance = 0.05f;
// Smallest share of a face's area its bake camera must see. Faces seen more edge-on than this hold
// almost no picture, and the sums that map a point back onto them are unstable.
inline constexpr float kBakedMinimumFacing = 0.04f;
// How much a quarter turn between a picture's camera and the view counts against that picture,
// against how squarely the picture saw the face (the dot product of the face normal and the camera
// direction). Small enough that a wall always uses its squarest picture, while the faces that every
// camera sees alike, such as flat roofs, use the picture nearest the view.
inline constexpr float kCompositeViewPreference = 0.1f;

namespace SC4BakedCompositeDetail
{
	// Largest number of corners a clipped fragment may have. A fragment that would exceed it is
	// left uncut, which keeps a little ghosting rather than losing part of the picture.
	inline constexpr int32_t kMaximumFragmentPoints = 16;

	// A point as seen from the bake camera.
	struct Point
	{
		float x;
		float y;
	};

	// A convex fragment with counterclockwise corners, as seen from the bake camera.
	struct Fragment
	{
		std::array<Point, kMaximumFragmentPoints> points;
		int32_t count = 0;

		/// <summary>
		/// Appends a corner.
		/// </summary>
		/// <param name="point">Corner to append.</param>
		/// <returns>False when the fragment is full.</returns>
		bool Add(const Point& point)
		{
			if (count >= kMaximumFragmentPoints) {
				return false;
			}
			points[static_cast<size_t>(count++)] = point;
			return true;
		}
	};

	// A front-facing triangle of the model, as seen from the bake camera.
	struct Face
	{
		// Mesh holding the triangle.
		uint32_t mesh;
		// Vertices of the triangle in the mesh.
		uint16_t vertex[3];
		// The triangle's corners in vertex order.
		Point corner[3];
		// The triangle's corners in counterclockwise order.
		Point ordered[3];
		// Twice the signed area in vertex order.
		float signedArea;
		// Depth toward the camera at corner 0.
		float depthOrigin;
		// Change in depth per unit of x.
		float depthPerX;
		// Change in depth per unit of y.
		float depthPerY;
		// Bounds as seen from the bake camera.
		float minX;
		float maxX;
		float minY;
		float maxY;
	};

	/// <summary>
	/// Returns the area of a fragment.
	/// </summary>
	/// <param name="fragment">Fragment to measure.</param>
	/// <returns>The area, which is positive for counterclockwise corners.</returns>
	inline float GetArea(const Fragment& fragment)
	{
		float doubled = 0.0f;
		for (int32_t i = 0; i < fragment.count; ++i) {
			const Point& p = fragment.points[static_cast<size_t>(i)];
			const Point& q = fragment.points[static_cast<size_t>((i + 1) % fragment.count)];
			doubled += p.x * q.y - q.x * p.y;
		}
		return doubled * 0.5f;
	}

	/// <summary>
	/// Splits a convex fragment along the line through two points.
	/// </summary>
	/// <param name="fragment">Fragment to split.</param>
	/// <param name="a">First point on the line.</param>
	/// <param name="b">Second point on the line.</param>
	/// <param name="left">Receives the part to the left of a to b.</param>
	/// <param name="right">Receives the part to the right of a to b.</param>
	/// <returns>False when a part would have too many corners.</returns>
	inline bool Split(const Fragment& fragment, const Point& a, const Point& b, Fragment& left, Fragment& right)
	{
		left.count = 0;
		right.count = 0;
		const float edgeX = b.x - a.x;
		const float edgeY = b.y - a.y;
		for (int32_t i = 0; i < fragment.count; ++i) {
			const Point& p = fragment.points[static_cast<size_t>(i)];
			const Point& q = fragment.points[static_cast<size_t>((i + 1) % fragment.count)];
			const float sideP = edgeX * (p.y - a.y) - edgeY * (p.x - a.x);
			const float sideQ = edgeX * (q.y - a.y) - edgeY * (q.x - a.x);
			if (sideP >= 0.0f && !left.Add(p)) {
				return false;
			}
			if (sideP <= 0.0f && !right.Add(p)) {
				return false;
			}
			if ((sideP > 0.0f && sideQ < 0.0f) || (sideP < 0.0f && sideQ > 0.0f)) {
				const float t = sideP / (sideP - sideQ);
				const Point crossing{ p.x + t * (q.x - p.x), p.y + t * (q.y - p.y) };
				if (!left.Add(crossing) || !right.Add(crossing)) {
					return false;
				}
			}
		}
		return true;
	}

	/// <summary>
	/// Returns a face's depth toward the camera at a point.
	/// </summary>
	/// <param name="face">Face to read.</param>
	/// <param name="x">Point's x as seen from the bake camera.</param>
	/// <param name="y">Point's y as seen from the bake camera.</param>
	/// <returns>The depth of the face's plane at that point.</returns>
	inline float GetDepth(const Face& face, float x, float y)
	{
		return face.depthOrigin + face.depthPerX * (x - face.corner[0].x) + face.depthPerY * (y - face.corner[0].y);
	}

	/// <summary>
	/// Removes from each of a face's fragments the part another face hides at the bake view.
	/// </summary>
	/// <param name="face">Face being clipped.</param>
	/// <param name="other">Face that may hide it.</param>
	/// <param name="otherComesFirst">Whether the other face keeps a surface the two share.</param>
	/// <param name="fragments">Fragments of the face, updated in place.</param>
	/// <param name="scratch">Reusable storage for the updated fragments.</param>
	inline void ClipAgainst(
		const Face& face,
		const Face& other,
		bool otherComesFirst,
		std::vector<Fragment>& fragments,
		std::vector<Fragment>& scratch)
	{
		scratch.clear();
		for (const Fragment& fragment : fragments) {
			Fragment rest = fragment;
			std::array<Fragment, 3> pieces;
			int32_t pieceCount = 0;
			bool usable = true;
			for (int32_t edge = 0; edge < 3 && rest.count >= 3; ++edge) {
				Fragment inside;
				Fragment outside;
				if (!Split(rest, other.ordered[edge], other.ordered[(edge + 1) % 3], inside, outside)) {
					usable = false;
					break;
				}
				if (outside.count >= 3 && std::abs(GetArea(outside)) > kBakedMinimumFragmentArea) {
					pieces[static_cast<size_t>(pieceCount++)] = outside;
				}
				rest = inside;
			}

			if (!usable || rest.count < 3 || std::abs(GetArea(rest)) <= kBakedMinimumFragmentArea) {
				scratch.push_back(fragment);
				continue;
			}

			float centerX = 0.0f;
			float centerY = 0.0f;
			for (int32_t i = 0; i < rest.count; ++i) {
				centerX += rest.points[static_cast<size_t>(i)].x;
				centerY += rest.points[static_cast<size_t>(i)].y;
			}
			centerX /= static_cast<float>(rest.count);
			centerY /= static_cast<float>(rest.count);

			const float depth = GetDepth(face, centerX, centerY);
			const float otherDepth = GetDepth(other, centerX, centerY);
			const bool hidden = otherDepth > depth + kBakedDepthTolerance
				|| (otherComesFirst && otherDepth >= depth - kBakedDepthTolerance);
			if (hidden) {
				for (int32_t i = 0; i < pieceCount; ++i) {
					scratch.push_back(pieces[static_cast<size_t>(i)]);
				}
			}
			else {
				scratch.push_back(fragment);
			}
		}
		fragments.swap(scratch);
	}
}

namespace SC4BakedCompositeDetail
{
	/// <summary>
	/// Returns the vertex at a point of a triangle. A point at a corner returns that corner's exact
	/// vertex, so triangles that shared a corner still do.
	/// </summary>
	/// <param name="point">Point on the triangle.</param>
	/// <param name="corner">The triangle's corners in vertex order.</param>
	/// <param name="v0">Vertex at corner 0.</param>
	/// <param name="v1">Vertex at corner 1.</param>
	/// <param name="v2">Vertex at corner 2.</param>
	/// <returns>The interpolated vertex.</returns>
	inline SC4BakedVertex InterpolateVertex(
		const Point& point,
		const Point corner[3],
		const SC4BakedVertex& v0,
		const SC4BakedVertex& v1,
		const SC4BakedVertex& v2)
	{
		if (point.x == corner[0].x && point.y == corner[0].y) {
			return v0;
		}
		if (point.x == corner[1].x && point.y == corner[1].y) {
			return v1;
		}
		if (point.x == corner[2].x && point.y == corner[2].y) {
			return v2;
		}

		const double ax = static_cast<double>(corner[1].x) - corner[0].x;
		const double ay = static_cast<double>(corner[1].y) - corner[0].y;
		const double bx = static_cast<double>(corner[2].x) - corner[0].x;
		const double by = static_cast<double>(corner[2].y) - corner[0].y;
		const double px = static_cast<double>(point.x) - corner[0].x;
		const double py = static_cast<double>(point.y) - corner[0].y;
		const double determinant = ax * by - bx * ay;
		const double w1 = (std::clamp)((px * by - bx * py) / determinant, 0.0, 1.0);
		const double w2 = (std::clamp)((ax * py - px * ay) / determinant, 0.0, 1.0 - w1);
		const double w0 = 1.0 - w1 - w2;

		SC4BakedVertex vertex;
		for (int32_t k = 0; k < 3; ++k) {
			vertex.position[k] = static_cast<float>(w0 * v0.position[k] + w1 * v1.position[k] + w2 * v2.position[k]);
		}
		vertex.uv[0] = static_cast<float>(w0 * v0.uv[0] + w1 * v1.uv[0] + w2 * v2.uv[0]);
		vertex.uv[1] = static_cast<float>(w0 * v0.uv[1] + w1 * v1.uv[1] + w2 * v2.uv[1]);
		return vertex;
	}
}

namespace SC4BakedCompositeDetail
{
	/// <summary>
	/// Returns the vertex at a point on a triangle's plane, which may lie a little outside the
	/// triangle. The picture is one flat mapping across the plane, so extending it is safe.
	/// </summary>
	/// <param name="point">Point on the plane.</param>
	/// <param name="corner">The triangle's corners in vertex order.</param>
	/// <param name="v0">Vertex at corner 0.</param>
	/// <param name="v1">Vertex at corner 1.</param>
	/// <param name="v2">Vertex at corner 2.</param>
	/// <returns>The extended vertex.</returns>
	inline SC4BakedVertex ExtendVertex(
		const Point& point,
		const Point corner[3],
		const SC4BakedVertex& v0,
		const SC4BakedVertex& v1,
		const SC4BakedVertex& v2)
	{
		const double ax = static_cast<double>(corner[1].x) - corner[0].x;
		const double ay = static_cast<double>(corner[1].y) - corner[0].y;
		const double bx = static_cast<double>(corner[2].x) - corner[0].x;
		const double by = static_cast<double>(corner[2].y) - corner[0].y;
		const double px = static_cast<double>(point.x) - corner[0].x;
		const double py = static_cast<double>(point.y) - corner[0].y;
		const double determinant = ax * by - bx * ay;
		const double w1 = (px * by - bx * py) / determinant;
		const double w2 = (ax * py - px * ay) / determinant;
		const double w0 = 1.0 - w1 - w2;

		SC4BakedVertex vertex;
		for (int32_t k = 0; k < 3; ++k) {
			vertex.position[k] = static_cast<float>(w0 * v0.position[k] + w1 * v1.position[k] + w2 * v2.position[k]);
		}
		vertex.uv[0] = static_cast<float>(w0 * v0.uv[0] + w1 * v1.uv[0] + w2 * v2.uv[0]);
		vertex.uv[1] = static_cast<float>(w0 * v0.uv[1] + w1 * v1.uv[1] + w2 * v2.uv[1]);
		return vertex;
	}

	/// <summary>
	/// Moves each edge of a convex fragment outward.
	/// </summary>
	/// <param name="fragment">Fragment with counterclockwise corners.</param>
	/// <param name="distance">Distance to move each edge.</param>
	/// <returns>The grown fragment.</returns>
	inline Fragment GrowFragment(const Fragment& fragment, float distance)
	{
		std::array<Point, kMaximumFragmentPoints> outward;
		for (int32_t i = 0; i < fragment.count; ++i) {
			const Point& p = fragment.points[static_cast<size_t>(i)];
			const Point& q = fragment.points[static_cast<size_t>((i + 1) % fragment.count)];
			const float dx = q.x - p.x;
			const float dy = q.y - p.y;
			const float length = std::sqrt(dx * dx + dy * dy);
			outward[static_cast<size_t>(i)] = length > 1e-6f ? Point{ dy / length, -dx / length } : Point{ 0.0f, 0.0f };
		}

		Fragment grown;
		grown.count = fragment.count;
		for (int32_t i = 0; i < fragment.count; ++i) {
			const Point& before = outward[static_cast<size_t>((i + fragment.count - 1) % fragment.count)];
			const Point& after = outward[static_cast<size_t>(i)];
			// A sharp corner would move far, so its movement is limited.
			const float scale = distance / (std::max)(1.0f + before.x * after.x + before.y * after.y, 0.25f);
			const Point& p = fragment.points[static_cast<size_t>(i)];
			grown.points[static_cast<size_t>(i)] = { p.x + (before.x + after.x) * scale, p.y + (before.y + after.y) * scale };
		}
		return grown;
	}
}

/// <summary>
/// Turns a model-space vector by quarter turns about +Y, in the direction that carries a vector
/// from one rotation variant into the variant before it. A vertex p of variant a is the point
/// TurnQuarter(p, a - b) in the model space of variant b. Measured on the meshes in
/// SimCity_1.dat, where variant c sees the building from azimuth 67.5 - 90c degrees.
/// </summary>
/// <param name="vector">Vector to turn.</param>
/// <param name="steps">Number of quarter turns, which may be negative.</param>
/// <param name="result">Receives the turned vector. May be the same array as vector.</param>
inline void TurnQuarter(const float vector[3], int32_t steps, float result[3])
{
	const float x = vector[0];
	const float y = vector[1];
	const float z = vector[2];
	switch (steps & 3) {
	case 0:
		result[0] = x;
		result[1] = y;
		result[2] = z;
		break;
	case 1:
		result[0] = z;
		result[1] = y;
		result[2] = -x;
		break;
	case 2:
		result[0] = -x;
		result[1] = y;
		result[2] = -z;
		break;
	default:
		result[0] = -z;
		result[1] = y;
		result[2] = x;
		break;
	}
}

// How a mesh's texture lies over its bake camera's view: each texture coordinate is an affine
// function of where a point appears to that camera.
struct SC4BakeMapping
{
	// u = u[0] * x + u[1] * y + u[2], for a point seen at x and y.
	double u[3] = {};
	// v = v[0] * x + v[1] * y + v[2].
	double v[3] = {};
	// Whether the mesh had enough vertices to measure the map.
	bool valid = false;
};

/// <summary>
/// Measures how a mesh's texture lies over its bake camera's view, and checks that the texture is
/// one picture taken by that camera, which is what makes the rest of this file valid: its texture
/// coordinates must depend only on where each vertex appears to the camera.
/// </summary>
/// <param name="mesh">Mesh to measure.</param>
/// <param name="bake">Axes of the camera the picture should have come from.</param>
/// <param name="tolerance">Largest error allowed in a texture coordinate.</param>
/// <param name="mapping">Receives the map. It stays invalid when there are too few vertices to measure it.</param>
/// <returns>True when the coordinates fit that camera's view, or there are too few vertices to tell.</returns>
inline bool FitBakeMapping(const SC4BakedMesh& mesh, const SC4CameraAxes& bake, float tolerance, SC4BakeMapping& mapping)
{
	mapping = SC4BakeMapping{};
	double xx = 0.0, xy = 0.0, x1 = 0.0, yy = 0.0, y1 = 0.0, n = 0.0;
	double xu = 0.0, yu = 0.0, u1 = 0.0, xv = 0.0, yv = 0.0, v1 = 0.0;
	std::vector<std::array<double, 2>> seen;
	seen.reserve(mesh.vertices.size());
	for (const SC4BakedVertex& vertex : mesh.vertices) {
		const double x = vertex.position[0] * bake.right[0] + vertex.position[1] * bake.right[1] + vertex.position[2] * bake.right[2];
		const double y = vertex.position[0] * bake.up[0] + vertex.position[1] * bake.up[1] + vertex.position[2] * bake.up[2];
		seen.push_back({ x, y });
		xx += x * x;
		xy += x * y;
		x1 += x;
		yy += y * y;
		y1 += y;
		n += 1.0;
		xu += x * vertex.uv[0];
		yu += y * vertex.uv[0];
		u1 += vertex.uv[0];
		xv += x * vertex.uv[1];
		yv += y * vertex.uv[1];
		v1 += vertex.uv[1];
	}

	const double determinant = xx * (yy * n - y1 * y1) - xy * (xy * n - y1 * x1) + x1 * (xy * y1 - yy * x1);
	if (std::abs(determinant) < 1e-6) {
		return true;
	}

	const auto solve = [&](double a, double b, double c, double out[3]) {
		out[0] = (a * (yy * n - y1 * y1) - xy * (b * n - y1 * c) + x1 * (b * y1 - yy * c)) / determinant;
		out[1] = (xx * (b * n - y1 * c) - a * (xy * n - y1 * x1) + x1 * (xy * c - b * x1)) / determinant;
		out[2] = (xx * (yy * c - b * y1) - xy * (xy * c - b * x1) + a * (xy * y1 - yy * x1)) / determinant;
	};
	solve(xu, yu, u1, mapping.u);
	solve(xv, yv, v1, mapping.v);

	for (size_t i = 0; i < mesh.vertices.size(); ++i) {
		const double u = mapping.u[0] * seen[i][0] + mapping.u[1] * seen[i][1] + mapping.u[2];
		const double v = mapping.v[0] * seen[i][0] + mapping.v[1] * seen[i][1] + mapping.v[2];
		if (std::abs(u - mesh.vertices[i].uv[0]) > tolerance || std::abs(v - mesh.vertices[i].uv[1]) > tolerance) {
			return false;
		}
	}
	mapping.valid = true;
	return true;
}

/// <summary>
/// Reduces a baked model to what its bake camera really saw of each face. All faces share one
/// picture, so a face behind another shows the front face's picture, which appears as a ghost
/// from any other direction. Removing the hidden parts loses nothing at the bake view, because
/// both faces show the same pixels there. Back faces are removed as well.
/// </summary>
/// <param name="model">Model to clip, updated in place.</param>
/// <param name="bake">Axes of the camera that baked the model.</param>
inline void ClipToBakeView(SC4BakedModel& model, const SC4CameraAxes& bake)
{
	using namespace SC4BakedCompositeDetail;

	std::vector<Face> faces;
	for (uint32_t m = 0; m < model.size(); ++m) {
		const SC4BakedMesh& mesh = model[m];
		for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
			const uint16_t index[3] = { mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2] };
			if (index[0] >= mesh.vertices.size() || index[1] >= mesh.vertices.size() || index[2] >= mesh.vertices.size()) {
				continue;
			}

			const float* p0 = mesh.vertices[index[0]].position;
			const float* p1 = mesh.vertices[index[1]].position;
			const float* p2 = mesh.vertices[index[2]].position;
			const float ex = p1[0] - p0[0], ey = p1[1] - p0[1], ez = p1[2] - p0[2];
			const float fx = p2[0] - p0[0], fy = p2[1] - p0[1], fz = p2[2] - p0[2];
			const float nx = ey * fz - ez * fy;
			const float ny = ez * fx - ex * fz;
			const float nz = ex * fy - ey * fx;
			const float doubledArea = std::sqrt(nx * nx + ny * ny + nz * nz);
			if (nx * bake.back[0] + ny * bake.back[1] + nz * bake.back[2] <= doubledArea * kBakedMinimumFacing) {
				continue;
			}

			Face face{};
			face.mesh = m;
			float depth[3];
			for (int32_t c = 0; c < 3; ++c) {
				const float* p = mesh.vertices[index[c]].position;
				face.vertex[c] = index[c];
				face.corner[c].x = p[0] * bake.right[0] + p[1] * bake.right[1] + p[2] * bake.right[2];
				face.corner[c].y = p[0] * bake.up[0] + p[1] * bake.up[1] + p[2] * bake.up[2];
				depth[c] = p[0] * bake.back[0] + p[1] * bake.back[1] + p[2] * bake.back[2];
			}

			const float ax = face.corner[1].x - face.corner[0].x;
			const float ay = face.corner[1].y - face.corner[0].y;
			const float bx = face.corner[2].x - face.corner[0].x;
			const float by = face.corner[2].y - face.corner[0].y;
			face.signedArea = ax * by - bx * ay;
			if (std::abs(face.signedArea) < 1e-6f) {
				continue;
			}

			const float da = depth[1] - depth[0];
			const float db = depth[2] - depth[0];
			face.depthOrigin = depth[0];
			face.depthPerX = (da * by - db * ay) / face.signedArea;
			face.depthPerY = (db * ax - da * bx) / face.signedArea;
			face.ordered[0] = face.corner[0];
			face.ordered[1] = face.signedArea > 0.0f ? face.corner[1] : face.corner[2];
			face.ordered[2] = face.signedArea > 0.0f ? face.corner[2] : face.corner[1];
			face.minX = face.maxX = face.corner[0].x;
			face.minY = face.maxY = face.corner[0].y;
			for (int32_t c = 1; c < 3; ++c) {
				face.minX = (std::min)(face.minX, face.corner[c].x);
				face.maxX = (std::max)(face.maxX, face.corner[c].x);
				face.minY = (std::min)(face.minY, face.corner[c].y);
				face.maxY = (std::max)(face.maxY, face.corner[c].y);
			}
			faces.push_back(face);
		}
	}

	SC4BakedModel clipped(model.size());
	std::vector<Fragment> fragments;
	std::vector<Fragment> scratch;
	for (size_t i = 0; i < faces.size(); ++i) {
		const Face& face = faces[i];
		Fragment whole;
		whole.Add(face.ordered[0]);
		whole.Add(face.ordered[1]);
		whole.Add(face.ordered[2]);
		fragments.clear();
		fragments.push_back(whole);

		for (size_t j = 0; j < faces.size() && !fragments.empty(); ++j) {
			const Face& other = faces[j];
			if (i == j
				|| other.minX >= face.maxX || other.maxX <= face.minX
				|| other.minY >= face.maxY || other.maxY <= face.minY) {
				continue;
			}
			ClipAgainst(face, other, j < i, fragments, scratch);
		}

		const SC4BakedMesh& source = model[face.mesh];
		SC4BakedMesh& target = clipped[face.mesh];
		const SC4BakedVertex& v0 = source.vertices[face.vertex[0]];
		const SC4BakedVertex& v1 = source.vertices[face.vertex[1]];
		const SC4BakedVertex& v2 = source.vertices[face.vertex[2]];
		for (const Fragment& fragment : fragments) {
			const size_t start = target.vertices.size();
			if (start + static_cast<size_t>(fragment.count) > 65535) {
				break;
			}
			for (int32_t c = 0; c < fragment.count; ++c) {
				target.vertices.push_back(InterpolateVertex(fragment.points[static_cast<size_t>(c)], face.corner, v0, v1, v2));
			}
			for (int32_t c = 1; c + 1 < fragment.count; ++c) {
				const uint16_t first = static_cast<uint16_t>(start);
				const uint16_t second = static_cast<uint16_t>(start + static_cast<size_t>(c));
				const uint16_t third = static_cast<uint16_t>(start + static_cast<size_t>(c) + 1);
				target.indices.push_back(first);
				target.indices.push_back(face.signedArea > 0.0f ? second : third);
				target.indices.push_back(face.signedArea > 0.0f ? third : second);
			}
		}
	}
	model.swap(clipped);
}

/// <summary>
/// Combines the four clipped rotation variants of a model into one complete building, drawn in
/// the model space of one variant. Each face uses the variant whose camera saw it most squarely,
/// which is its sharpest and best lit picture, and faces every camera saw alike, such as flat
/// roofs, use the variant nearest the view. The variants share one low-detail shape, so several of them often have a picture of
/// the same surface. The depth buffer cannot separate coplanar surfaces, so each lower preference
/// is cut away wherever a higher one covers the plane, leaving only the parts nothing else saw.
/// </summary>
/// <param name="variants">The four clipped variants, each in its own model space.</param>
/// <param name="currentVariant">Variant whose model space the result uses.</param>
/// <param name="bake">Axes of the camera that baked the variants, in their own model space.</param>
/// <param name="meshes">Receives the meshes that have anything left to draw.</param>
/// <param name="viewPreference">How much a quarter turn away from the view counts against a picture.</param>
inline void ComposeVariants(
	const std::array<SC4BakedModel, 4>& variants,
	int32_t currentVariant,
	const SC4CameraAxes& bake,
	std::vector<SC4CompositeMesh>& meshes,
	float viewPreference = kCompositeViewPreference)
{
	using namespace SC4BakedCompositeDetail;

	// One triangle on its way into the composite, in the current variant's model space.
	struct Triangle
	{
		int32_t rank;
		size_t target;
		SC4BakedVertex vertex[3];
		float normal[3];
		float planeDistance;
		Point corner[3];
		Point ordered[3];
		float signedArea;
		float minX;
		float maxX;
		float minY;
		float maxY;
	};

	meshes.clear();

	// How far each variant's camera is from the current one, in quarter turns.
	static constexpr int32_t kViewDistance[4] = { 0, 1, 2, 1 };
	static constexpr int32_t kVariantOrder[4] = { 0, 1, 3, 2 };

	std::vector<Triangle> triangles;
	for (const int32_t relative : kVariantOrder) {
		const int32_t variant = (currentVariant + relative) & 3;
		const SC4BakedModel& model = variants[static_cast<size_t>(variant)];
		for (uint32_t m = 0; m < model.size(); ++m) {
			const SC4BakedMesh& source = model[m];
			if (source.indices.empty()) {
				continue;
			}

			const size_t target = meshes.size();
			SC4CompositeMesh composite{};
			composite.variant = variant;
			composite.meshIndex = m;
			meshes.push_back(std::move(composite));

			for (size_t i = 0; i + 2 < source.indices.size(); i += 3) {
				const float* p0 = source.vertices[source.indices[i]].position;
				const float* p1 = source.vertices[source.indices[i + 1]].position;
				const float* p2 = source.vertices[source.indices[i + 2]].position;
				const float ex = p1[0] - p0[0], ey = p1[1] - p0[1], ez = p1[2] - p0[2];
				const float fx = p2[0] - p0[0], fy = p2[1] - p0[1], fz = p2[2] - p0[2];
				float normal[3] = { ey * fz - ez * fy, ez * fx - ex * fz, ex * fy - ey * fx };
				const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
				if (length <= 1e-6f) {
					continue;
				}
				normal[0] /= length;
				normal[1] /= length;
				normal[2] /= length;

				// Each variant's camera saw this face as squarely as its normal faces that camera.
				// Nearness to the view only breaks near ties, such as for a flat roof.
				float score[4];
				for (int32_t step = 0; step < 4; ++step) {
					float direction[3];
					TurnQuarter(bake.back, step, direction);
					const int32_t other = (variant + step) & 3;
					score[step] = normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2]
						- viewPreference * static_cast<float>(kViewDistance[(other - currentVariant) & 3]);
				}
				int32_t rank = 0;
				for (int32_t step = 1; step < 4; ++step) {
					const int32_t other = (variant + step) & 3;
					if (score[step] > score[0] + 0.0001f
						|| (score[step] >= score[0] - 0.0001f
							&& kViewDistance[(other - currentVariant) & 3] < kViewDistance[relative])) {
						++rank;
					}
				}

				Triangle triangle{};
				triangle.rank = rank;
				triangle.target = target;
				for (size_t c = 0; c < 3; ++c) {
					const SC4BakedVertex& from = source.vertices[source.indices[i + c]];
					triangle.vertex[c] = from;
					TurnQuarter(from.position, relative, triangle.vertex[c].position);
				}
				TurnQuarter(normal, relative, triangle.normal);
				triangle.planeDistance = triangle.normal[0] * triangle.vertex[0].position[0]
					+ triangle.normal[1] * triangle.vertex[0].position[1]
					+ triangle.normal[2] * triangle.vertex[0].position[2];
				triangles.push_back(triangle);
			}
		}
	}

	// Group the triangles by plane, and within a plane by preference.
	std::vector<std::vector<uint32_t>> planes;
	for (uint32_t t = 0; t < triangles.size(); ++t) {
		const Triangle& triangle = triangles[t];
		std::vector<uint32_t>* plane = nullptr;
		for (std::vector<uint32_t>& candidate : planes) {
			const Triangle& other = triangles[candidate.front()];
			const float agreement = triangle.normal[0] * other.normal[0]
				+ triangle.normal[1] * other.normal[1]
				+ triangle.normal[2] * other.normal[2];
			if (agreement >= kCompositeSamePlaneNormal
				&& std::abs(triangle.planeDistance - other.planeDistance) <= kCompositeSamePlaneDistance) {
				plane = &candidate;
				break;
			}
		}
		if (plane) {
			plane->push_back(t);
		}
		else {
			planes.push_back({ t });
		}
	}

	std::vector<Fragment> fragments;
	std::vector<Fragment> scratch;
	for (std::vector<uint32_t>& plane : planes) {
		std::stable_sort(plane.begin(), plane.end(), [&](uint32_t a, uint32_t b) {
			return triangles[a].rank < triangles[b].rank;
		});

		// Axes of the plane, for working in two dimensions.
		const float* n = triangles[plane.front()].normal;
		float axisX[3];
		if (std::abs(n[1]) < 0.9f) {
			axisX[0] = n[2];
			axisX[1] = 0.0f;
			axisX[2] = -n[0];
		}
		else {
			axisX[0] = 0.0f;
			axisX[1] = -n[2];
			axisX[2] = n[1];
		}
		const float axisLength = std::sqrt(axisX[0] * axisX[0] + axisX[1] * axisX[1] + axisX[2] * axisX[2]);
		axisX[0] /= axisLength;
		axisX[1] /= axisLength;
		axisX[2] /= axisLength;
		const float axisY[3] = {
			n[1] * axisX[2] - n[2] * axisX[1],
			n[2] * axisX[0] - n[0] * axisX[2],
			n[0] * axisX[1] - n[1] * axisX[0],
		};

		for (const uint32_t t : plane) {
			Triangle& triangle = triangles[t];
			for (size_t c = 0; c < 3; ++c) {
				const float* p = triangle.vertex[c].position;
				triangle.corner[c].x = p[0] * axisX[0] + p[1] * axisX[1] + p[2] * axisX[2];
				triangle.corner[c].y = p[0] * axisY[0] + p[1] * axisY[1] + p[2] * axisY[2];
			}
			const float ax = triangle.corner[1].x - triangle.corner[0].x;
			const float ay = triangle.corner[1].y - triangle.corner[0].y;
			const float bx = triangle.corner[2].x - triangle.corner[0].x;
			const float by = triangle.corner[2].y - triangle.corner[0].y;
			triangle.signedArea = ax * by - bx * ay;
			triangle.ordered[0] = triangle.corner[0];
			triangle.ordered[1] = triangle.signedArea > 0.0f ? triangle.corner[1] : triangle.corner[2];
			triangle.ordered[2] = triangle.signedArea > 0.0f ? triangle.corner[2] : triangle.corner[1];
			triangle.minX = (std::min)({ triangle.corner[0].x, triangle.corner[1].x, triangle.corner[2].x });
			triangle.maxX = (std::max)({ triangle.corner[0].x, triangle.corner[1].x, triangle.corner[2].x });
			triangle.minY = (std::min)({ triangle.corner[0].y, triangle.corner[1].y, triangle.corner[2].y });
			triangle.maxY = (std::max)({ triangle.corner[0].y, triangle.corner[1].y, triangle.corner[2].y });
		}

		for (size_t a = 0; a < plane.size(); ++a) {
			const Triangle& triangle = triangles[plane[a]];
			if (std::abs(triangle.signedArea) < 1e-6f) {
				continue;
			}

			Fragment whole;
			whole.Add(triangle.ordered[0]);
			whole.Add(triangle.ordered[1]);
			whole.Add(triangle.ordered[2]);
			fragments.clear();
			fragments.push_back(whole);

			for (size_t b = 0; b < a && !fragments.empty(); ++b) {
				const Triangle& other = triangles[plane[b]];
				if (other.rank >= triangle.rank
					|| other.minX >= triangle.maxX || other.maxX <= triangle.minX
					|| other.minY >= triangle.maxY || other.maxY <= triangle.minY
					|| std::abs(other.signedArea) < 1e-6f) {
					continue;
				}

				scratch.clear();
				for (const Fragment& fragment : fragments) {
					Fragment rest = fragment;
					std::array<Fragment, 3> pieces;
					int32_t pieceCount = 0;
					bool usable = true;
					for (int32_t edge = 0; edge < 3 && rest.count >= 3; ++edge) {
						Fragment inside;
						Fragment outside;
						if (!Split(rest, other.ordered[edge], other.ordered[(edge + 1) % 3], inside, outside)) {
							usable = false;
							break;
						}
						if (outside.count >= 3 && std::abs(GetArea(outside)) > kBakedMinimumFragmentArea) {
							pieces[static_cast<size_t>(pieceCount++)] = outside;
						}
						rest = inside;
					}

					if (!usable || rest.count < 3 || std::abs(GetArea(rest)) <= kBakedMinimumFragmentArea) {
						scratch.push_back(fragment);
					}
					else {
						for (int32_t i = 0; i < pieceCount; ++i) {
							scratch.push_back(pieces[static_cast<size_t>(i)]);
						}
					}
				}
				fragments.swap(scratch);
			}

			SC4CompositeMesh& target = meshes[triangle.target];
			for (const Fragment& fragment : fragments) {
				const size_t start = target.vertices.size();
				if (start + static_cast<size_t>(fragment.count) > 65535) {
					break;
				}
				const Fragment grown = GrowFragment(fragment, kCompositeSeamOverlap);
				for (int32_t c = 0; c < grown.count; ++c) {
					target.vertices.push_back(ExtendVertex(
						grown.points[static_cast<size_t>(c)],
						triangle.corner,
						triangle.vertex[0],
						triangle.vertex[1],
						triangle.vertex[2]));
				}
				for (int32_t c = 1; c + 1 < fragment.count; ++c) {
					const uint16_t first = static_cast<uint16_t>(start);
					const uint16_t second = static_cast<uint16_t>(start + static_cast<size_t>(c));
					const uint16_t third = static_cast<uint16_t>(start + static_cast<size_t>(c) + 1);
					target.indices.push_back(first);
					target.indices.push_back(triangle.signedArea > 0.0f ? second : third);
					target.indices.push_back(triangle.signedArea > 0.0f ? third : second);
				}
			}
		}
	}

	meshes.erase(
		std::remove_if(meshes.begin(), meshes.end(), [](const SC4CompositeMesh& mesh) { return mesh.indices.empty(); }),
		meshes.end());
}
