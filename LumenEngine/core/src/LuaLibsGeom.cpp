// lumen.geom: intersection tests, polygons, splines
#include "LuaVec.h"

#include <algorithm>
#include <array>

namespace lumen::lua::detail
{
	namespace
	{
		struct P3 { double x, y, z; };
		P3 Sub(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
		double Dot(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		P3 Cross(P3 a, P3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
		P3 Arg3(lua_State* L, int i) { double v[4]; CheckVec(L, i, 3, v); return {v[0], v[1], v[2]}; }
		struct P2 { double x, y; };
		P2 Arg2(lua_State* L, int i) { double v[4]; CheckVec(L, i, 2, v); return {v[0], v[1]}; }
		void PushNil(lua_State* L) { lua_pushnil(L); }

		int RayPlane(lua_State* L)
		{
			const P3 o = Arg3(L, 1), d = Arg3(L, 2), p = Arg3(L, 3), n = Arg3(L, 4);
			const double den = Dot(n, d);
			if (std::fabs(den) < 1e-12) { PushNil(L); return 1; }
			const double t = Dot(n, Sub(p, o)) / den;
			if (t < 0) PushNil(L); else lua_pushnumber(L, t);
			return 1;
		}
		int RaySphere(lua_State* L)
		{
			const P3 o = Arg3(L, 1), d = Arg3(L, 2), c = Arg3(L, 3);
			const double r = luaL_checknumber(L, 4);
			const P3 oc = Sub(o, c);
			const double a = Dot(d, d), b = 2 * Dot(oc, d), cc = Dot(oc, oc) - r * r, disc = b * b - 4 * a * cc;
			if (disc < 0 || a < 1e-18) { PushNil(L); return 1; }
			const double s = std::sqrt(disc);
			double t = (-b - s) / (2 * a);
			if (t < 0) t = (-b + s) / (2 * a);
			if (t < 0) PushNil(L); else lua_pushnumber(L, t);
			return 1;
		}
		int RayAabb(lua_State* L)
		{
			const P3 o = Arg3(L, 1), d = Arg3(L, 2), mn = Arg3(L, 3), mx = Arg3(L, 4);
			double tmin = 0, tmax = 1e300;
			const double oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z}, lo[3] = {mn.x, mn.y, mn.z}, hi[3] = {mx.x, mx.y, mx.z};
			for (int i = 0; i < 3; ++i)
			{
				if (std::fabs(dd[i]) < 1e-15)
				{
					if (oo[i] < lo[i] || oo[i] > hi[i]) { PushNil(L); return 1; }
					continue;
				}
				double t1 = (lo[i] - oo[i]) / dd[i], t2 = (hi[i] - oo[i]) / dd[i];
				if (t1 > t2) std::swap(t1, t2);
				tmin = std::max(tmin, t1);
				tmax = std::min(tmax, t2);
				if (tmin > tmax) { PushNil(L); return 1; }
			}
			lua_pushnumber(L, tmin);
			lua_pushnumber(L, tmax);
			return 2;
		}
		// Moeller-Trumbore. returns t, u, v
		int RayTriangle(lua_State* L)
		{
			const P3 o = Arg3(L, 1), d = Arg3(L, 2), a = Arg3(L, 3), b = Arg3(L, 4), c = Arg3(L, 5);
			const bool cull = OptBool(L, 6, false);
			const P3 e1 = Sub(b, a), e2 = Sub(c, a), pv = Cross(d, e2);
			const double det = Dot(e1, pv);
			if (cull ? det < 1e-12 : std::fabs(det) < 1e-12) { PushNil(L); return 1; }
			const double inv = 1.0 / det;
			const P3 tv = Sub(o, a);
			const double u = Dot(tv, pv) * inv;
			if (u < 0 || u > 1) { PushNil(L); return 1; }
			const P3 qv = Cross(tv, e1);
			const double v = Dot(d, qv) * inv;
			if (v < 0 || u + v > 1) { PushNil(L); return 1; }
			const double t = Dot(e2, qv) * inv;
			if (t < 0) { PushNil(L); return 1; }
			lua_pushnumber(L, t); lua_pushnumber(L, u); lua_pushnumber(L, v);
			return 3;
		}
		int AabbOverlap(lua_State* L)
		{
			const P3 a0 = Arg3(L, 1), a1 = Arg3(L, 2), b0 = Arg3(L, 3), b1 = Arg3(L, 4);
			lua_pushboolean(L, a0.x <= b1.x && a1.x >= b0.x && a0.y <= b1.y && a1.y >= b0.y && a0.z <= b1.z && a1.z >= b0.z);
			return 1;
		}
		int AabbContains(lua_State* L)
		{
			const P3 a = Arg3(L, 1), b = Arg3(L, 2), p = Arg3(L, 3);
			lua_pushboolean(L, p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y && p.z >= a.z && p.z <= b.z);
			return 1;
		}
		int SphereOverlap(lua_State* L)
		{
			const P3 a = Arg3(L, 1), b = Arg3(L, 3);
			const double r = luaL_checknumber(L, 2) + luaL_checknumber(L, 4);
			const P3 d = Sub(a, b);
			lua_pushboolean(L, Dot(d, d) <= r * r);
			return 1;
		}
		int SphereAabb(lua_State* L)
		{
			const P3 c = Arg3(L, 1);
			const double r = luaL_checknumber(L, 2);
			const P3 mn = Arg3(L, 3), mx = Arg3(L, 4);
			const P3 q = {std::max(mn.x, std::min(c.x, mx.x)), std::max(mn.y, std::min(c.y, mx.y)), std::max(mn.z, std::min(c.z, mx.z))};
			const P3 d = Sub(c, q);
			lua_pushboolean(L, Dot(d, d) <= r * r);
			return 1;
		}
		int ClosestOnSegment(lua_State* L)
		{
			double p[4], a[4], b[4];
			int n = 2;
			CheckVec(L, 1, 2, p, &n);
			int na = 2, nb = 2;
			CheckVec(L, 2, 2, a, &na);
			CheckVec(L, 3, 2, b, &nb);
			const int dim = std::max({n, na, nb}) >= 3 ? 3 : 2;
			double ab[3] = {0, 0, 0}, ap[3] = {0, 0, 0}, len = 0, dot = 0;
			for (int i = 0; i < dim; ++i) { ab[i] = b[i] - a[i]; ap[i] = p[i] - a[i]; len += ab[i] * ab[i]; dot += ap[i] * ab[i]; }
			const double t = len < 1e-18 ? 0 : std::max(0.0, std::min(1.0, dot / len));
			PushVec(L, dim, a[0] + ab[0] * t, a[1] + ab[1] * t, dim == 3 ? a[2] + ab[2] * t : 0);
			lua_pushnumber(L, t);
			return 2;
		}
		int SegmentIntersect2(lua_State* L)
		{
			const P2 a = Arg2(L, 1), b = Arg2(L, 2), c = Arg2(L, 3), d = Arg2(L, 4);
			const double r_x = b.x - a.x, r_y = b.y - a.y, s_x = d.x - c.x, s_y = d.y - c.y;
			const double den = r_x * s_y - r_y * s_x;
			if (std::fabs(den) < 1e-14) { PushNil(L); return 1; }
			const double t = ((c.x - a.x) * s_y - (c.y - a.y) * s_x) / den, u = ((c.x - a.x) * r_y - (c.y - a.y) * r_x) / den;
			if (t < 0 || t > 1 || u < 0 || u > 1) { PushNil(L); return 1; }
			PushVec2(L, a.x + t * r_x, a.y + t * r_y);
			lua_pushnumber(L, t);
			return 2;
		}
		std::vector<P2> Poly(lua_State* L, int idx)
		{
			luaL_checktype(L, idx, LUA_TTABLE);
			std::vector<P2> pts;
			const lua_Integer n = (lua_Integer)luaL_len(L, idx);
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, idx, i);
				pts.push_back(Arg2(L, -1));
				lua_pop(L, 1);
			}
			return pts;
		}
		double SignedArea(const std::vector<P2>& p)
		{
			double a = 0;
			for (size_t i = 0; i < p.size(); ++i)
			{
				const P2 &c = p[i], &n = p[(i + 1) % p.size()];
				a += c.x * n.y - n.x * c.y;
			}
			return a / 2;
		}
		int PointInPolygon(lua_State* L)
		{
			const P2 pt = Arg2(L, 1);
			const std::vector<P2> p = Poly(L, 2);
			bool in = false;
			for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
				if (((p[i].y > pt.y) != (p[j].y > pt.y)) && (pt.x < (p[j].x - p[i].x) * (pt.y - p[i].y) / (p[j].y - p[i].y) + p[i].x))
					in = !in;
			lua_pushboolean(L, in);
			return 1;
		}
		int PolygonArea(lua_State* L) { lua_pushnumber(L, SignedArea(Poly(L, 1))); return 1; }
		int PolygonCentroid(lua_State* L)
		{
			const std::vector<P2> p = Poly(L, 1);
			double a = 0, cx = 0, cy = 0;
			for (size_t i = 0; i < p.size(); ++i)
			{
				const P2 &c = p[i], &n = p[(i + 1) % p.size()];
				const double cr = c.x * n.y - n.x * c.y;
				a += cr;
				cx += (c.x + n.x) * cr;
				cy += (c.y + n.y) * cr;
			}
			if (std::fabs(a) < 1e-14)
			{
				double sx = 0, sy = 0;
				for (const P2& q : p) { sx += q.x; sy += q.y; }
				PushVec2(L, p.empty() ? 0 : sx / (double)p.size(), p.empty() ? 0 : sy / (double)p.size());
			}
			else
				PushVec2(L, cx / (3 * a), cy / (3 * a));
			return 1;
		}
		int ConvexHull(lua_State* L)
		{
			std::vector<P2> p = Poly(L, 1);
			std::sort(p.begin(), p.end(), [](P2 a, P2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
			p.erase(std::unique(p.begin(), p.end(), [](P2 a, P2 b) { return a.x == b.x && a.y == b.y; }), p.end());
			if (p.size() < 3)
			{
				lua_createtable(L, (int)p.size(), 0);
				for (size_t i = 0; i < p.size(); ++i) { PushVec2(L, p[i].x, p[i].y); lua_rawseti(L, -2, (lua_Integer)i + 1); }
				return 1;
			}
			auto cross = [](P2 o, P2 a, P2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
			std::vector<P2> h(2 * p.size());
			size_t k = 0;
			for (size_t i = 0; i < p.size(); ++i)
			{
				while (k >= 2 && cross(h[k - 2], h[k - 1], p[i]) <= 0) --k;
				h[k++] = p[i];
			}
			for (size_t i = p.size() - 1, t = k + 1; i > 0; --i)
			{
				while (k >= t && cross(h[k - 2], h[k - 1], p[i - 1]) <= 0) --k;
				h[k++] = p[i - 1];
			}
			h.resize(k - 1);
			lua_createtable(L, (int)h.size(), 0);
			for (size_t i = 0; i < h.size(); ++i) { PushVec2(L, h[i].x, h[i].y); lua_rawseti(L, -2, (lua_Integer)i + 1); }
			return 1;
		}
		// ear clipping; returns a flat array of 1-based indices (3 per triangle)
		int Triangulate(lua_State* L)
		{
			const std::vector<P2> p = Poly(L, 1);
			const size_t n = p.size();
			lua_newtable(L);
			if (n < 3) return 1;
			std::vector<size_t> idx(n);
			for (size_t i = 0; i < n; ++i) idx[i] = i;
			if (SignedArea(p) < 0) std::reverse(idx.begin(), idx.end());
			auto cross = [&](size_t a, size_t b, size_t c) { return (p[b].x - p[a].x) * (p[c].y - p[a].y) - (p[b].y - p[a].y) * (p[c].x - p[a].x); };
			auto inTri = [&](size_t a, size_t b, size_t c, size_t q) {
				return cross(a, b, q) >= 0 && cross(b, c, q) >= 0 && cross(c, a, q) >= 0;
			};
			lua_Integer out = 1;
			size_t guard = 0;
			while (idx.size() > 3 && guard++ < n * n)
			{
				bool clipped = false;
				for (size_t i = 0; i < idx.size(); ++i)
				{
					const size_t a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
					if (cross(a, b, c) <= 1e-14) continue;
					bool ear = true;
					for (size_t q : idx)
						if (q != a && q != b && q != c && inTri(a, b, c, q)) { ear = false; break; }
					if (!ear) continue;
					for (size_t v : {a, b, c}) { lua_pushinteger(L, (lua_Integer)v + 1); lua_rawseti(L, -2, out++); }
					idx.erase(idx.begin() + (long)i);
					clipped = true;
					break;
				}
				if (!clipped) break;
			}
			if (idx.size() == 3)
				for (size_t v : idx) { lua_pushinteger(L, (lua_Integer)v + 1); lua_rawseti(L, -2, out++); }
			return 1;
		}
		// Ramer-Douglas-Peucker on 2D/3D polylines
		int Simplify(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const double eps = luaL_checknumber(L, 2);
			std::vector<P3> pts;
			int dim = 2;
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				double v[4];
				int nn = 2;
				CheckVec(L, -1, 2, v, &nn);
				if (nn >= 3) dim = 3;
				pts.push_back({v[0], v[1], v[2]});
				lua_pop(L, 1);
			}
			std::vector<bool> keep(pts.size(), false);
			if (!pts.empty()) { keep[0] = true; keep[pts.size() - 1] = true; }
			std::vector<std::pair<size_t, size_t>> stack;
			if (pts.size() > 2) stack.push_back({0, pts.size() - 1});
			while (!stack.empty())
			{
				auto [s, e] = stack.back();
				stack.pop_back();
				double best = -1;
				size_t bi = 0;
				const P3 ab = Sub(pts[e], pts[s]);
				const double l2 = Dot(ab, ab);
				for (size_t i = s + 1; i < e; ++i)
				{
					const P3 ap = Sub(pts[i], pts[s]);
					double t = l2 < 1e-18 ? 0 : std::max(0.0, std::min(1.0, Dot(ap, ab) / l2));
					const P3 d = {ap.x - ab.x * t, ap.y - ab.y * t, ap.z - ab.z * t};
					const double dist = std::sqrt(Dot(d, d));
					if (dist > best) { best = dist; bi = i; }
				}
				if (best > eps) { keep[bi] = true; stack.push_back({s, bi}); stack.push_back({bi, e}); }
			}
			lua_newtable(L);
			lua_Integer o = 1;
			for (size_t i = 0; i < pts.size(); ++i)
				if (keep[i]) { PushVec(L, dim, pts[i].x, pts[i].y, pts[i].z); lua_rawseti(L, -2, o++); }
			return 1;
		}
		// de Casteljau for any number of control points (vec2/vec3/vec4)
		int Bezier(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const double t = luaL_checknumber(L, 2);
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			luaL_argcheck(L, n >= 2, 1, "need at least 2 control points");
			std::vector<std::array<double, 4>> pts((size_t)n);
			int dim = 2;
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				double v[4];
				int nn = 2;
				CheckVec(L, -1, 2, v, &nn);
				dim = std::max(dim, nn);
				pts[(size_t)i - 1] = {v[0], v[1], v[2], v[3]};
				lua_pop(L, 1);
			}
			for (size_t level = pts.size() - 1; level > 0; --level)
				for (size_t i = 0; i < level; ++i)
					for (int k = 0; k < 4; ++k) pts[i][k] = pts[i][k] + (pts[i + 1][k] - pts[i][k]) * t;
			PushVec(L, dim, pts[0][0], pts[0][1], pts[0][2], pts[0][3]);
			return 1;
		}
		// Catmull-Rom through all points; t in [0,1] over the entire spline. alpha 0 uniform, 0.5 centripetal.
		int CatmullSpline(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const double t = luaL_checknumber(L, 2);
			const bool closed = OptBool(L, 3, false);
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			luaL_argcheck(L, n >= 2, 1, "need at least 2 points");
			std::vector<std::array<double, 4>> pts((size_t)n);
			int dim = 2;
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				double v[4];
				int nn = 2;
				CheckVec(L, -1, 2, v, &nn);
				dim = std::max(dim, nn);
				pts[(size_t)i - 1] = {v[0], v[1], v[2], v[3]};
				lua_pop(L, 1);
			}
			const size_t segs = closed ? pts.size() : pts.size() - 1;
			double ft = std::max(0.0, std::min(1.0, t)) * (double)segs;
			size_t seg = std::min((size_t)ft, segs - 1);
			const double u = ft - (double)seg;
			auto at = [&](long i) -> const std::array<double, 4>& {
				if (closed) return pts[(size_t)(((i % (long)pts.size()) + (long)pts.size()) % (long)pts.size())];
				return pts[(size_t)std::max(0L, std::min((long)pts.size() - 1, i))];
			};
			const auto &p0 = at((long)seg - 1), &p1 = at((long)seg), &p2 = at((long)seg + 1), &p3 = at((long)seg + 2);
			double r[4];
			for (int k = 0; k < 4; ++k)
				r[k] = 0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * u + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * u * u + (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * u * u * u);
			PushVec(L, dim, r[0], r[1], r[2], r[3]);
			return 1;
		}
		int LineCircle(lua_State* L)
		{
			const P2 a = Arg2(L, 1), b = Arg2(L, 2), c = Arg2(L, 3);
			const double r = luaL_checknumber(L, 4);
			const double dx = b.x - a.x, dy = b.y - a.y, fx = a.x - c.x, fy = a.y - c.y;
			const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), C = fx * fx + fy * fy - r * r, disc = B * B - 4 * A * C;
			lua_newtable(L);
			if (disc < 0 || A < 1e-18) return 1;
			const double s = std::sqrt(disc);
			lua_Integer i = 1;
			for (double t : {(-B - s) / (2 * A), (-B + s) / (2 * A)})
				if (t >= 0 && t <= 1)
				{
					lua_pushnumber(L, t);
					lua_rawseti(L, -2, i++);
				}
			return 1;
		}
		int CircleOverlap(lua_State* L)
		{
			const P2 a = Arg2(L, 1), b = Arg2(L, 3);
			const double r = luaL_checknumber(L, 2) + luaL_checknumber(L, 4), dx = a.x - b.x, dy = a.y - b.y;
			lua_pushboolean(L, dx * dx + dy * dy <= r * r);
			return 1;
		}
		int RectOverlap(lua_State* L)
		{
			const double ax = luaL_checknumber(L, 1), ay = luaL_checknumber(L, 2), aw = luaL_checknumber(L, 3), ah = luaL_checknumber(L, 4);
			const double bx = luaL_checknumber(L, 5), by = luaL_checknumber(L, 6), bw = luaL_checknumber(L, 7), bh = luaL_checknumber(L, 8);
			lua_pushboolean(L, ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
			return 1;
		}
		int Barycentric(lua_State* L)
		{
			const P3 p = Arg3(L, 1), a = Arg3(L, 2), b = Arg3(L, 3), c = Arg3(L, 4);
			const P3 v0 = Sub(b, a), v1 = Sub(c, a), v2 = Sub(p, a);
			const double d00 = Dot(v0, v0), d01 = Dot(v0, v1), d11 = Dot(v1, v1), d20 = Dot(v2, v0), d21 = Dot(v2, v1), den = d00 * d11 - d01 * d01;
			if (std::fabs(den) < 1e-18) { PushNil(L); return 1; }
			const double v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
			PushVec3(L, 1 - v - w, v, w);
			return 1;
		}
		int TriangleNormal(lua_State* L)
		{
			const P3 a = Arg3(L, 1), b = Arg3(L, 2), c = Arg3(L, 3);
			P3 n = Cross(Sub(b, a), Sub(c, a));
			const double l = std::sqrt(Dot(n, n));
			if (l > 1e-18) n = {n.x / l, n.y / l, n.z / l};
			PushVec3(L, n.x, n.y, n.z);
			return 1;
		}
		// Bounding box of a list of vec2/vec3 -> min, max
		int Bounds(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			if (n == 0) return Fail(L, "empty point list");
			double mn[3] = {1e300, 1e300, 1e300}, mx[3] = {-1e300, -1e300, -1e300};
			int dim = 2;
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				double v[4];
				int nn = 2;
				CheckVec(L, -1, 2, v, &nn);
				dim = std::max(dim, nn);
				for (int k = 0; k < 3; ++k) { mn[k] = std::min(mn[k], v[k]); mx[k] = std::max(mx[k], v[k]); }
				lua_pop(L, 1);
			}
			PushVec(L, dim, mn[0], mn[1], mn[2]);
			PushVec(L, dim, mx[0], mx[1], mx[2]);
			return 2;
		}
	} // namespace

	int OpenGeom(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"ray_plane", RayPlane}, {"ray_sphere", RaySphere}, {"ray_aabb", RayAabb}, {"ray_triangle", RayTriangle},
			{"aabb_overlap", AabbOverlap}, {"aabb_contains", AabbContains}, {"sphere_overlap", SphereOverlap}, {"sphere_aabb", SphereAabb},
			{"closest_on_segment", ClosestOnSegment}, {"segment_intersect2", SegmentIntersect2}, {"point_in_polygon", PointInPolygon},
			{"polygon_area", PolygonArea}, {"polygon_centroid", PolygonCentroid}, {"convex_hull", ConvexHull}, {"triangulate", Triangulate},
			{"simplify", Simplify}, {"bezier", Bezier}, {"catmull_rom_spline", CatmullSpline}, {"line_circle", LineCircle}, {"circle_overlap", CircleOverlap},
			{"rect_overlap", RectOverlap}, {"barycentric", Barycentric}, {"triangle_normal", TriangleNormal}, {"bounds", Bounds}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
