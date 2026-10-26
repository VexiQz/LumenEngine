#include "lumen/core/Navigation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace lumen::nav
{
	namespace
	{
		struct Hit
		{
			float ymin, ymax;
			bool walkable;
		};

		Float3 Sub(const Float3& a, const Float3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
		Float3 Cross(const Float3& a, const Float3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
		float Len(const Float3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

		// Separating axis test of a triangle (projected to XZ) against a square.
		bool TriSquare(const float tx[3], const float tz[3], float cx, float cz, float h)
		{
			if (std::max({tx[0], tx[1], tx[2]}) < cx - h || std::min({tx[0], tx[1], tx[2]}) > cx + h) return false;
			if (std::max({tz[0], tz[1], tz[2]}) < cz - h || std::min({tz[0], tz[1], tz[2]}) > cz + h) return false;
			for (int i = 0; i < 3; ++i)
			{
				const int j = (i + 1) % 3;
				const float nx = -(tz[j] - tz[i]), nz = tx[j] - tx[i];
				float lo = 1e30f, hi = -1e30f;
				for (int k = 0; k < 3; ++k)
				{
					const float d = tx[k] * nx + tz[k] * nz;
					lo = std::min(lo, d);
					hi = std::max(hi, d);
				}
				const float c = cx * nx + cz * nz;
				const float r = h * (std::fabs(nx) + std::fabs(nz));
				if (hi < c - r || lo > c + r) return false;
			}
			return true;
		}
	}

	void NavMesh::Clear()
	{
		built_ = false;
		spans_.clear();
		columnStart_.clear();
		blockers_.clear();
		width_ = depth_ = 0;
		stats_ = {};
	}

	bool NavMesh::Build(const std::vector<Triangle>& triangles, const BuildSettings& s)
	{
		const auto t0 = std::chrono::steady_clock::now();
		Clear();
		settings_ = s;
		cell_ = std::max(0.02f, s.cellSize);
		const float sx = s.boundsMax.x - s.boundsMin.x, sz = s.boundsMax.z - s.boundsMin.z;
		if (sx <= 0 || sz <= 0)
			return false;
		while ((double)std::ceil(sx / cell_) * (double)std::ceil(sz / cell_) > (double)s.maxColumns)
			cell_ *= 1.25f;
		width_ = std::max(1, (int)std::ceil(sx / cell_));
		depth_ = std::max(1, (int)std::ceil(sz / cell_));
		originX_ = s.boundsMin.x;
		originZ_ = s.boundsMin.z;

		const size_t columns = (size_t)width_ * (size_t)depth_;
		std::vector<std::vector<Hit>> hits(columns);
		const float cosSlope = std::cos(s.maxSlope * 3.14159265f / 180.0f);
		const float h = cell_ * 0.5f;

		for (const Triangle& t : triangles)
		{
			const Float3 e1 = Sub(t.b, t.a), e2 = Sub(t.c, t.a);
			Float3 n = Cross(e1, e2);
			const float len = Len(n);
			if (len < 1e-8f)
				continue;
			n = {n.x / len, n.y / len, n.z / len};
			const float ymin = std::min({t.a.y, t.b.y, t.c.y}), ymax = std::max({t.a.y, t.b.y, t.c.y});
			if (ymax < s.boundsMin.y || ymin > s.boundsMax.y)
				continue;
			const float tx[3] = {t.a.x, t.b.x, t.c.x}, tz[3] = {t.a.z, t.b.z, t.c.z};
			const int x0 = std::max(0, (int)std::floor((std::min({tx[0], tx[1], tx[2]}) - originX_) / cell_));
			const int x1 = std::min(width_ - 1, (int)std::floor((std::max({tx[0], tx[1], tx[2]}) - originX_) / cell_));
			const int z0 = std::max(0, (int)std::floor((std::min({tz[0], tz[1], tz[2]}) - originZ_) / cell_));
			const int z1 = std::min(depth_ - 1, (int)std::floor((std::max({tz[0], tz[1], tz[2]}) - originZ_) / cell_));
			const bool walkable = n.y >= cosSlope;
			const bool flat = std::fabs(n.y) > 1e-4f;
			for (int z = z0; z <= z1; ++z)
				for (int x = x0; x <= x1; ++x)
				{
					const float cx = originX_ + (x + 0.5f) * cell_, cz = originZ_ + (z + 0.5f) * cell_;
					if (!TriSquare(tx, tz, cx, cz, h))
						continue;
					Hit hit{ymin, ymax, walkable};
					if (flat)
					{
						float y = t.a.y - (n.x * (cx - t.a.x) + n.z * (cz - t.a.z)) / n.y;
						y = std::min(std::max(y, ymin), ymax);
						if (walkable)
							hit = {y, y, true};
						else if (std::fabs(n.y) > 0.05f)
						{
							const float half = h * (std::fabs(n.x) + std::fabs(n.z)) / std::fabs(n.y);
							hit = {std::max(ymin, y - half), std::min(ymax, y + half), false};
							if (hit.ymin > hit.ymax)
								hit.ymin = hit.ymax = y;
						}
					}
					hits[(size_t)z * (size_t)width_ + (size_t)x].push_back(hit);
				}
		}

		// spans: walkable hits with enough free space above them
		columnStart_.assign(columns + 1, 0);
		spans_.clear();
		for (size_t c = 0; c < columns; ++c)
		{
			columnStart_[c] = (int)spans_.size();
			std::vector<Hit>& hv = hits[c];
			std::sort(hv.begin(), hv.end(), [](const Hit& a, const Hit& b) { return a.ymax < b.ymax; });
			float lastY = -1e30f;
			for (size_t i = 0; i < hv.size(); ++i)
			{
				if (!hv[i].walkable)
					continue;
				const float y = hv[i].ymax;
				if (y < s.boundsMin.y || y > s.boundsMax.y || y - lastY < 0.05f)
					continue;
				bool blocked = false;
				for (size_t k = 0; k < hv.size() && !blocked; ++k)
					if (k != i && hv[k].ymax > y + s.stepHeight && hv[k].ymin < y + s.agentHeight && !(hv[k].walkable && hv[k].ymax - y < 0.05f))
						blocked = true;
				if (blocked)
					continue;
				spans_.push_back({y, true, 0});
				lastY = y;
			}
			std::vector<Hit>().swap(hv);
		}
		columnStart_[columns] = (int)spans_.size();

		// erosion by the agent radius
		const int r = (int)std::ceil(s.agentRadius / cell_);
		const float tanSlope = std::tan(s.maxSlope * 3.14159265f / 180.0f);
		if (r > 0)
		{
			std::vector<unsigned char> erode(spans_.size(), 0);
			for (int z = 0; z < depth_; ++z)
				for (int x = 0; x < width_; ++x)
					for (int si = 0; si < Count(x, z); ++si)
					{
						const float y = At(x, z, si).y;
						bool bad = false;
						for (int dz = -r; dz <= r && !bad; ++dz)
							for (int dx = -r; dx <= r && !bad; ++dx)
							{
								if (dx == 0 && dz == 0) continue;
								const float d2 = (float)(dx * dx + dz * dz);
								if (d2 > (float)(r * r) + 0.25f) continue;
								const int nx = x + dx, nz = z + dz;
								bool found = false;
								if (nx >= 0 && nz >= 0 && nx < width_ && nz < depth_)
								{
									const float allowed = s.stepHeight + std::sqrt(d2) * cell_ * tanSlope;
									for (int k = 0; k < Count(nx, nz); ++k)
										if (std::fabs(At(nx, nz, k).y - y) <= allowed) { found = true; break; }
								}
								if (!found) bad = true;
							}
						if (bad) erode[(size_t)columnStart_[(size_t)Index(x, z)] + (size_t)si] = 1;
					}
			for (size_t i = 0; i < spans_.size(); ++i)
				if (erode[i]) spans_[i].walkable = false;
		}

		stats_ = {};
		stats_.columns = (int)columns;
		stats_.spans = (int)spans_.size();
		for (const Span& sp : spans_) stats_.walkable += sp.walkable ? 1 : 0;
		stats_.width = width_;
		stats_.depth = depth_;
		stats_.cellSize = cell_;
		built_ = stats_.walkable > 0;
		stats_.bakeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		return built_;
	}

	// ---- helpers -------------------------------------------------------------------------------------------
	bool NavMesh::Cell(const Float3& p, int& x, int& z) const
	{
		x = (int)std::floor((p.x - originX_) / cell_);
		z = (int)std::floor((p.z - originZ_) / cell_);
		return x >= 0 && z >= 0 && x < width_ && z < depth_;
	}

	Float3 NavMesh::Pos(int x, int z, int s) const
	{
		return {originX_ + (x + 0.5f) * cell_, At(x, z, s).y, originZ_ + (z + 0.5f) * cell_};
	}

	// Closest usable span of a column to height y (-1 when none within maxDy).
	int NavMesh::FindSpan(int x, int z, float y, float maxDy) const
	{
		int best = -1;
		float bestD = maxDy;
		for (int s = 0; s < Count(x, z); ++s)
		{
			const Span& sp = At(x, z, s);
			if (!sp.walkable || sp.blockers > 0) continue;
			const float d = std::fabs(sp.y - y);
			if (d <= bestD) { bestD = d; best = s; }
		}
		return best;
	}

	int NavMesh::Neighbour(int x, int z, int s, int dx, int dz) const
	{
		const int nx = x + dx, nz = z + dz;
		if (nx < 0 || nz < 0 || nx >= width_ || nz >= depth_) return -1;
		const float dist = (dx != 0 && dz != 0) ? 1.41421356f : 1.0f;
		const float allowed = settings_.stepHeight + dist * cell_ * std::tan(settings_.maxSlope * 3.14159265f / 180.0f);
		const int n = FindSpan(nx, nz, At(x, z, s).y, allowed);
		if (n < 0) return -1;
		if (dx != 0 && dz != 0)
		{
			// no corner cutting: both orthogonal steps must exist
			const int a = Neighbour(x, z, s, dx, 0), b = Neighbour(x, z, s, 0, dz);
			if (a < 0 || b < 0) return -1;
		}
		return n;
	}

	bool NavMesh::Project(const Float3& p, Float3& out, float maxDistance) const
	{
		int cx, cz, s;
		if (!built_) return false;
		// allow points slightly outside the grid
		cx = (int)std::floor((p.x - originX_) / cell_);
		cz = (int)std::floor((p.z - originZ_) / cell_);
		const int R = std::max(1, (int)std::ceil(maxDistance / cell_));
		float best = 1e30f;
		int bx = -1, bz = -1, bs = -1;
		for (int dz = -R; dz <= R; ++dz)
			for (int dx = -R; dx <= R; ++dx)
			{
				const int x = cx + dx, z = cz + dz;
				if (x < 0 || z < 0 || x >= width_ || z >= depth_) continue;
				for (s = 0; s < Count(x, z); ++s)
				{
					const Span& sp = At(x, z, s);
					if (!sp.walkable || sp.blockers > 0) continue;
					const float px = originX_ + (x + 0.5f) * cell_ - p.x, pz = originZ_ + (z + 0.5f) * cell_ - p.z;
					const float horiz = std::sqrt(px * px + pz * pz);
					const float score = horiz + 2.0f * std::fabs(sp.y - p.y);
					if (horiz <= maxDistance + cell_ && std::fabs(sp.y - p.y) <= maxDistance * 2 && score < best) { best = score; bx = x; bz = z; bs = s; }
				}
			}
		if (bs < 0) return false;
		out = Pos(bx, bz, bs);
		// keep the requested horizontal position when it is inside the same cell
		if (cx == bx && cz == bz) { out.x = p.x; out.z = p.z; }
		return true;
	}

	bool NavMesh::Raycast(const Float3& from, const Float3& to, Float3* hit) const
	{
		if (!built_) return false;
		Float3 start;
		if (!Project(from, start, cell_ * 2.0f)) return false;
		const float dx = to.x - start.x, dz = to.z - start.z;
		const float dist = std::sqrt(dx * dx + dz * dz);
		const int steps = std::max(1, (int)std::ceil(dist / (cell_ * 0.5f)));
		float lastY = start.y;
		Float3 last = start;
		for (int i = 1; i <= steps; ++i)
		{
			const float t = (float)i / (float)steps;
			const float px = start.x + dx * t, pz = start.z + dz * t;
			int x, z;
			if (!Cell({px, 0, pz}, x, z)) { if (hit) *hit = last; return false; }
			const float allowed = settings_.stepHeight + cell_ * 0.5f * std::tan(settings_.maxSlope * 3.14159265f / 180.0f) + 0.01f;
			const int s = FindSpan(x, z, lastY, allowed);
			if (s < 0) { if (hit) *hit = last; return false; }
			lastY = At(x, z, s).y;
			last = {px, lastY, pz};
		}
		if (hit) *hit = last;
		return true;
	}

	bool NavMesh::RandomPoint(const Float3& center, float radius, std::mt19937& rng, Float3& out) const
	{
		if (!built_) return false;
		std::uniform_real_distribution<float> u(0.0f, 1.0f);
		for (int i = 0; i < 24; ++i)
		{
			const float a = u(rng) * 6.2831853f, d = radius * std::sqrt(u(rng));
			const Float3 p{center.x + std::cos(a) * d, center.y, center.z + std::sin(a) * d};
			Float3 q;
			if (Project(p, q, cell_ * 1.5f) && std::fabs(q.y - center.y) < std::max(radius, 3.0f))
			{
				out = q;
				return true;
			}
		}
		return false;
	}

	// ---- path finding -----------------------------------------------------------------------------------------
	bool NavMesh::FindPath(const Float3& from, const Float3& to, std::vector<Float3>& out, bool allowPartial, bool* partial) const
	{
		out.clear();
		if (partial) *partial = false;
		if (!built_) return false;
		const float reach = std::max(2.0f, cell_ * 4.0f);
		Float3 ps, pg;
		if (!Project(from, ps, reach)) return false;
		const bool goalOk = Project(to, pg, reach);
		if (!goalOk && !allowPartial) return false;

		auto locate = [&](const Float3& p, int& x, int& z, int& s) {
			Cell(p, x, z);
			s = FindSpan(x, z, p.y, 0.6f);
			return s >= 0;
		};
		int sx, sz, ss, gx = 0, gz = 0, gs = 0;
		if (!locate(ps, sx, sz, ss)) return false;
		const bool haveGoal = goalOk && locate(pg, gx, gz, gs);
		if (!haveGoal && !allowPartial) return false;

		struct Node { float g; int parent; int x, z, s; bool closed; };
		std::unordered_map<int, Node> nodes;
		using QItem = std::pair<float, int>;
		std::priority_queue<QItem, std::vector<QItem>, std::greater<QItem>> open;
		auto key = [&](int x, int z, int s) { return columnStart_[(size_t)Index(x, z)] + s; };
		const Float3 target = haveGoal ? Pos(gx, gz, gs) : to;
		auto heuristic = [&](int x, int z, int s) {
			const Float3 p = Pos(x, z, s);
			const float dx = std::fabs(p.x - target.x), dz = std::fabs(p.z - target.z), dy = std::fabs(p.y - target.y);
			return std::max(dx, dz) + 0.41421356f * std::min(dx, dz) + dy * 0.5f;
		};
		const int startKey = key(sx, sz, ss), goalKey = haveGoal ? key(gx, gz, gs) : -1;
		nodes[startKey] = {0, -1, sx, sz, ss, false};
		open.push({heuristic(sx, sz, ss), startKey});
		int bestKey = startKey;
		float bestH = heuristic(sx, sz, ss);
		int foundKey = -1;
		static const int D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
		while (!open.empty())
		{
			const int k = open.top().second;
			open.pop();
			Node& n = nodes[k];
			if (n.closed) continue;
			n.closed = true;
			if (k == goalKey) { foundKey = k; break; }
			const float h = heuristic(n.x, n.z, n.s);
			if (h < bestH) { bestH = h; bestKey = k; }
			const Float3 cp = Pos(n.x, n.z, n.s);
			for (const auto& d : D)
			{
				const int ns = Neighbour(n.x, n.z, n.s, d[0], d[1]);
				if (ns < 0) continue;
				const int nx = n.x + d[0], nz = n.z + d[1];
				const int nk = key(nx, nz, ns);
				const Float3 np = Pos(nx, nz, ns);
				const float dx = np.x - cp.x, dy = np.y - cp.y, dz = np.z - cp.z;
				const float g = n.g + std::sqrt(dx * dx + dy * dy + dz * dz);
				auto it = nodes.find(nk);
				if (it != nodes.end() && (it->second.closed || it->second.g <= g)) continue;
				nodes[nk] = {g, k, nx, nz, ns, false};
				open.push({g + heuristic(nx, nz, ns), nk});
			}
		}
		int endKey = foundKey;
		if (endKey < 0)
		{
			if (!allowPartial || bestKey == startKey) return false;
			endKey = bestKey;
			if (partial) *partial = true;
		}
		std::vector<Float3> raw;
		for (int k = endKey; k >= 0; k = nodes[k].parent)
		{
			const Node& n = nodes[k];
			raw.push_back(Pos(n.x, n.z, n.s));
		}
		std::reverse(raw.begin(), raw.end());
		raw.front() = ps;
		if (foundKey >= 0) raw.back() = pg;
		Smooth(raw);
		out = std::move(raw);
		return true;
	}

	void NavMesh::Smooth(std::vector<Float3>& path) const
	{
		if (path.size() <= 2) return;
		std::vector<Float3> res;
		res.push_back(path.front());
		size_t i = 0;
		while (i + 1 < path.size())
		{
			size_t j = path.size() - 1;
			while (j > i + 1)
			{
				Float3 hit;
				const Float3 a = path[i], b = path[j];
				bool clear = Raycast(a, b, &hit);
				if (clear && std::fabs(hit.y - b.y) > settings_.stepHeight + 0.2f) clear = false;
				if (clear) break;
				--j;
			}
			res.push_back(path[j]);
			i = j;
		}
		path = std::move(res);
	}

	// ---- blockers -------------------------------------------------------------------------------------------------
	int NavMesh::AddBlocker(const Float3& bmin, const Float3& bmax)
	{
		if (!built_) return 0;
		Blocker b{nextBlocker_++, bmin, bmax, true};
		blockers_.push_back(b);
		const int x0 = std::max(0, (int)std::floor((bmin.x - originX_) / cell_)), x1 = std::min(width_ - 1, (int)std::floor((bmax.x - originX_) / cell_));
		const int z0 = std::max(0, (int)std::floor((bmin.z - originZ_) / cell_)), z1 = std::min(depth_ - 1, (int)std::floor((bmax.z - originZ_) / cell_));
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
				for (int s = 0; s < Count(x, z); ++s)
				{
					Span& sp = spans_[(size_t)columnStart_[(size_t)Index(x, z)] + (size_t)s];
					if (sp.y >= bmin.y - settings_.stepHeight && sp.y <= bmax.y) sp.blockers++;
				}
		return b.id;
	}

	void NavMesh::RemoveBlocker(int id)
	{
		for (Blocker& b : blockers_)
		{
			if (b.id != id || !b.alive) continue;
			b.alive = false;
			const int x0 = std::max(0, (int)std::floor((b.min.x - originX_) / cell_)), x1 = std::min(width_ - 1, (int)std::floor((b.max.x - originX_) / cell_));
			const int z0 = std::max(0, (int)std::floor((b.min.z - originZ_) / cell_)), z1 = std::min(depth_ - 1, (int)std::floor((b.max.z - originZ_) / cell_));
			for (int z = z0; z <= z1; ++z)
				for (int x = x0; x <= x1; ++x)
					for (int s = 0; s < Count(x, z); ++s)
					{
						Span& sp = spans_[(size_t)columnStart_[(size_t)Index(x, z)] + (size_t)s];
						if (sp.y >= b.min.y - settings_.stepHeight && sp.y <= b.max.y && sp.blockers > 0) sp.blockers--;
					}
		}
	}

	// ---- debug geometry ---------------------------------------------------------------------------------------------
	void NavMesh::DebugTriangles(std::vector<Triangle>& out, float lift) const
	{
		for (int z = 0; z < depth_; ++z)
			for (int x = 0; x < width_; ++x)
				for (int s = 0; s < Count(x, z); ++s)
				{
					if (!At(x, z, s).walkable) continue;
					const float y = At(x, z, s).y + lift;
					const float x0 = originX_ + x * cell_, z0 = originZ_ + z * cell_, x1 = x0 + cell_, z1 = z0 + cell_;
					out.push_back({{x0, y, z0}, {x0, y, z1}, {x1, y, z1}});
					out.push_back({{x0, y, z0}, {x1, y, z1}, {x1, y, z0}});
				}
	}

	void NavMesh::DebugBorderLines(std::vector<Float3>& out, float lift) const
	{
		static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (int z = 0; z < depth_; ++z)
			for (int x = 0; x < width_; ++x)
				for (int s = 0; s < Count(x, z); ++s)
				{
					const Span& sp = At(x, z, s);
					if (!sp.walkable) continue;
					const float y = sp.y + lift;
					const float x0 = originX_ + x * cell_, z0 = originZ_ + z * cell_, x1 = x0 + cell_, z1 = z0 + cell_;
					for (int i = 0; i < 4; ++i)
					{
						if (Neighbour(x, z, s, D[i][0], D[i][1]) >= 0) continue;
						if (i == 0) { out.push_back({x1, y, z0}); out.push_back({x1, y, z1}); }
						else if (i == 1) { out.push_back({x0, y, z0}); out.push_back({x0, y, z1}); }
						else if (i == 2) { out.push_back({x0, y, z1}); out.push_back({x1, y, z1}); }
						else { out.push_back({x0, y, z0}); out.push_back({x1, y, z0}); }
					}
				}
	}

} // namespace lumen::nav
