// Lumen Engine — navigation mesh: baked from world space triangles into a walkable span grid, A* path finding with
// string-pulling style smoothing, projection, line-of-sight and random point queries. Pure CPU, no engine dependencies.
//
// Coordinate system: +Y up (left handed world, but nothing here depends on handedness).
#pragma once

#include "lumen/core/Common.h"

#include <cstdint>
#include <functional>
#include <random>

namespace lumen::nav
{
	struct Triangle
	{
		Float3 a, b, c;
	};

	struct BuildSettings
	{
		float cellSize = 0.35f;
		float agentRadius = 0.4f;
		float agentHeight = 1.8f;
		float maxSlope = 45.0f;   // degrees
		float stepHeight = 0.45f; // maximum climbable ledge
		Float3 boundsMin{-40, -15, -40};
		Float3 boundsMax{40, 15, 40};
		size_t maxColumns = 4000000; // cellSize grows automatically when the volume would exceed this
	};

	struct Stats
	{
		int columns = 0, spans = 0, walkable = 0, width = 0, depth = 0;
		float cellSize = 0;
		double bakeMilliseconds = 0;
	};

	class NavMesh
	{
	public:
		// Builds the mesh from world space triangles (everything inside the bounds is considered: walkable surfaces by
		// slope, the rest blocks). Returns false when nothing walkable was found.
		bool Build(const std::vector<Triangle>& triangles, const BuildSettings& settings);
		void Clear();
		bool Built() const { return built_; }
		const Stats& GetStats() const { return stats_; }
		const BuildSettings& Settings() const { return settings_; }

		// Nearest walkable point to `p` within `maxDistance` (horizontal ring search, vertical preference).
		bool Project(const Float3& p, Float3& out, float maxDistance = 2.0f) const;
		// Shortest path; both end points are projected first. `out` holds waypoints (start .. goal) in world space.
		// Returns false when no route exists; `partial` (optional) receives true when the path ends at the closest
		// reachable point instead of the goal.
		bool FindPath(const Float3& from, const Float3& to, std::vector<Float3>& out, bool allowPartial = false, bool* partial = nullptr) const;
		// Walks the straight segment on the mesh; false (and `hit` = last walkable point) when it leaves the surface.
		bool Raycast(const Float3& from, const Float3& to, Float3* hit = nullptr) const;
		// Uniformly distributed walkable point around `center`.
		bool RandomPoint(const Float3& center, float radius, std::mt19937& rng, Float3& out) const;

		// Dynamic blockers (doors, crates): cells covered by the box are unwalkable until removed. Returns an id.
		int AddBlocker(const Float3& boxMin, const Float3& boxMax);
		void RemoveBlocker(int id);

		// Debug geometry: every walkable cell as a quad (two triangles), `lift` meters above the surface.
		void DebugTriangles(std::vector<Triangle>& out, float lift = 0.05f) const;
		// Border edges of the walkable area (pairs of points) — much cheaper to draw than DebugTriangles.
		void DebugBorderLines(std::vector<Float3>& out, float lift = 0.05f) const;

	private:
		struct Span
		{
			float y = 0;
			bool walkable = false;
			int blockers = 0;   // number of dynamic blockers covering the span
		};
		int Index(int x, int z) const { return z * width_ + x; }
		bool Cell(const Float3& p, int& x, int& z) const;
		int FindSpan(int x, int z, float y, float maxDy) const;
		int Neighbour(int x, int z, int s, int dx, int dz) const;
		Float3 Pos(int x, int z, int s) const;
		bool Blocked(int x, int z, int s) const { return spans_[(size_t)columnStart_[(size_t)Index(x, z)] + (size_t)s].blockers > 0; }
		const Span& At(int x, int z, int s) const { return spans_[(size_t)columnStart_[(size_t)Index(x, z)] + (size_t)s]; }
		int Count(int x, int z) const { return columnStart_[(size_t)Index(x, z) + 1] - columnStart_[(size_t)Index(x, z)]; }
		void Smooth(std::vector<Float3>& path) const;

		bool built_ = false;
		BuildSettings settings_;
		Stats stats_;
		int width_ = 0, depth_ = 0;
		float cell_ = 0.35f;
		float originX_ = 0, originZ_ = 0;
		std::vector<int> columnStart_; // size width*depth+1, prefix sums into spans_
		std::vector<Span> spans_;
		struct Blocker { int id; Float3 min, max; bool alive; };
		std::vector<Blocker> blockers_;
		int nextBlocker_ = 1;
	};

} // namespace lumen::nav
