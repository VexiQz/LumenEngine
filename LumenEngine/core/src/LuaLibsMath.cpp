// lumen.mathx: scalar helpers, easing, vec2/vec3/vec4, quat, mat4
#include "LuaVec.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <map>

namespace lumen::lua::detail
{
	namespace
	{
		const double kPi = 3.14159265358979323846;
		const double kEps = 1e-12;
		const char* kVec2 = "lumen.vec2";
		const char* kVec3 = "lumen.vec3";
		const char* kVec4 = "lumen.vec4";
		const char* kQuat = "lumen.quat";
		const char* kMat4 = "lumen.mat4";

		struct Quat { double x, y, z, w; };
		struct Mat4 { double m[16]; }; // column-major: element(row,col) = m[col*4 + row]

		double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
	} // namespace

	const char* VecMeta(int n) { return n == 2 ? kVec2 : (n == 3 ? kVec3 : kVec4); }

	void PushVec(lua_State* L, int n, double x, double y, double z, double w)
	{
		Vec* v = static_cast<Vec*>(lua_newuserdatauv(L, sizeof(Vec), 0));
		v->n = n;
		v->v[0] = x; v->v[1] = y; v->v[2] = z; v->v[3] = w;
		luaL_setmetatable(L, VecMeta(n));
	}

	bool ToVec(lua_State* L, int idx, int minN, double out[4], int* nOut)
	{
		idx = lua_absindex(L, idx);
		out[0] = out[1] = out[2] = out[3] = 0;
		if (lua_type(L, idx) == LUA_TUSERDATA)
		{
			for (int n = 2; n <= 4; ++n)
			{
				if (void* p = luaL_testudata(L, idx, VecMeta(n)))
				{
					const Vec* v = static_cast<Vec*>(p);
					if (v->n < minN)
						return false;
					for (int i = 0; i < 4; ++i)
						out[i] = v->v[i];
					if (nOut) *nOut = n;
					return true;
				}
			}
			return false;
		}
		if (lua_istable(L, idx))
		{
			static const char* names[4] = {"x", "y", "z", "w"};
			int count = 0;
			for (int i = 0; i < 4; ++i)
			{
				lua_getfield(L, idx, names[i]);
				if (!lua_isnumber(L, -1))
				{
					lua_pop(L, 1);
					lua_rawgeti(L, idx, i + 1);
				}
				if (lua_isnumber(L, -1))
				{
					out[i] = lua_tonumber(L, -1);
					count = i + 1;
				}
				lua_pop(L, 1);
			}
			if (count < minN)
				return false;
			if (nOut) *nOut = count;
			return true;
		}
		return false;
	}

	void CheckVec(lua_State* L, int idx, int minN, double out[4], int* nOut)
	{
		if (!ToVec(L, idx, minN, out, nOut))
			luaL_argerror(L, idx, StrFormat("expected vec%d (or table with x,y%s)", minN, minN >= 3 ? ",z" : "").c_str());
	}

	namespace
	{
		// ------------------------------------------------------------ scalar helpers
		double Lerp(double a, double b, double t) { return a + (b - a) * t; }
		int SLerp(lua_State* L) { lua_pushnumber(L, Lerp(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3))); return 1; }
		int SInvLerp(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), v = luaL_checknumber(L, 3);
			lua_pushnumber(L, std::fabs(b - a) < kEps ? 0.0 : (v - a) / (b - a));
			return 1;
		}
		int SRemap(lua_State* L)
		{
			const double v = luaL_checknumber(L, 1), a = luaL_checknumber(L, 2), b = luaL_checknumber(L, 3), c = luaL_checknumber(L, 4), d = luaL_checknumber(L, 5);
			const double t = std::fabs(b - a) < kEps ? 0.0 : (v - a) / (b - a);
			lua_pushnumber(L, Lerp(c, d, OptBool(L, 6, false) ? Clamp(t, 0, 1) : t));
			return 1;
		}
		int SClamp(lua_State* L) { lua_pushnumber(L, Clamp(luaL_checknumber(L, 1), luaL_optnumber(L, 2, 0), luaL_optnumber(L, 3, 1))); return 1; }
		int SSaturate(lua_State* L) { lua_pushnumber(L, Clamp(luaL_checknumber(L, 1), 0, 1)); return 1; }
		int SSmoothstep(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2);
			const double t = Clamp((luaL_checknumber(L, 3) - a) / (b - a), 0, 1);
			lua_pushnumber(L, t * t * (3 - 2 * t));
			return 1;
		}
		int SSmootherstep(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2);
			const double t = Clamp((luaL_checknumber(L, 3) - a) / (b - a), 0, 1);
			lua_pushnumber(L, t * t * t * (t * (t * 6 - 15) + 10));
			return 1;
		}
		int SWrap(lua_State* L)
		{
			const double v = luaL_checknumber(L, 1), lo = luaL_checknumber(L, 2), hi = luaL_checknumber(L, 3);
			const double r = hi - lo;
			lua_pushnumber(L, r <= 0 ? lo : lo + std::fmod(std::fmod(v - lo, r) + r, r));
			return 1;
		}
		int SPingPong(lua_State* L)
		{
			const double t = luaL_checknumber(L, 1), len = luaL_checknumber(L, 2);
			if (len <= 0) { lua_pushnumber(L, 0); return 1; }
			const double m = std::fmod(std::fmod(t, len * 2) + len * 2, len * 2);
			lua_pushnumber(L, len - std::fabs(m - len));
			return 1;
		}
		int SSign(lua_State* L) { const double v = luaL_checknumber(L, 1); lua_pushinteger(L, v > 0 ? 1 : (v < 0 ? -1 : 0)); return 1; }
		int SApproach(lua_State* L)
		{
			const double c = luaL_checknumber(L, 1), t = luaL_checknumber(L, 2), s = std::fabs(luaL_checknumber(L, 3));
			lua_pushnumber(L, c < t ? std::min(c + s, t) : std::max(c - s, t));
			return 1;
		}
		int SDamp(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), lambda = luaL_checknumber(L, 3), dt = luaL_checknumber(L, 4);
			lua_pushnumber(L, Lerp(a, b, 1 - std::exp(-lambda * dt)));
			return 1;
		}
		// Unity style critically damped smoothing. returns value, velocity
		int SSmoothDamp(lua_State* L)
		{
			double cur = luaL_checknumber(L, 1), target = luaL_checknumber(L, 2), vel = luaL_checknumber(L, 3);
			const double smooth = std::max(0.0001, luaL_checknumber(L, 4)), dt = luaL_checknumber(L, 5);
			const double maxSpeed = luaL_optnumber(L, 6, 1e30);
			const double omega = 2.0 / smooth, x = omega * dt;
			const double e = 1.0 / (1.0 + x + 0.48 * x * x + 0.235 * x * x * x);
			double change = cur - target;
			const double orig = target;
			const double maxChange = maxSpeed * smooth;
			change = Clamp(change, -maxChange, maxChange);
			target = cur - change;
			const double temp = (vel + omega * change) * dt;
			vel = (vel - omega * temp) * e;
			double out = target + (change + temp) * e;
			if ((orig - cur > 0) == (out > orig))
			{
				out = orig;
				vel = dt > 0 ? (out - orig) / dt : 0;
			}
			lua_pushnumber(L, out);
			lua_pushnumber(L, vel);
			return 2;
		}
		// spring(position, velocity, target, stiffness, damping, dt) -> position, velocity (semi-implicit, stable substeps)
		int SSpring(lua_State* L)
		{
			double p = luaL_checknumber(L, 1), v = luaL_checknumber(L, 2);
			const double target = luaL_checknumber(L, 3), k = luaL_checknumber(L, 4), d = luaL_checknumber(L, 5), dt = luaL_checknumber(L, 6);
			const int steps = std::max(1, (int)std::ceil(dt / 0.004));
			const double h = dt / steps;
			for (int i = 0; i < steps; ++i)
			{
				v += (-k * (p - target) - d * v) * h;
				p += v * h;
			}
			lua_pushnumber(L, p);
			lua_pushnumber(L, v);
			return 2;
		}
		int SDeg(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * 180.0 / kPi); return 1; }
		int SRad(lua_State* L) { lua_pushnumber(L, luaL_checknumber(L, 1) * kPi / 180.0); return 1; }
		int SRound(lua_State* L)
		{
			const double v = luaL_checknumber(L, 1);
			const lua_Integer d = luaL_optinteger(L, 2, 0);
			const double p = std::pow(10.0, (double)d);
			lua_pushnumber(L, std::floor(v * p + 0.5) / p);
			return 1;
		}
		int SApprox(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), e = luaL_optnumber(L, 3, 1e-6);
			lua_pushboolean(L, std::fabs(a - b) <= e * std::max(1.0, std::max(std::fabs(a), std::fabs(b))));
			return 1;
		}
		int SFract(lua_State* L) { const double v = luaL_checknumber(L, 1); lua_pushnumber(L, v - std::floor(v)); return 1; }
		int SAngleDiff(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2);
			double d = std::fmod(b - a, 2 * kPi);
			if (d > kPi) d -= 2 * kPi;
			if (d < -kPi) d += 2 * kPi;
			lua_pushnumber(L, d);
			return 1;
		}
		int SLerpAngle(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), t = luaL_checknumber(L, 3);
			double d = std::fmod(b - a, 2 * kPi);
			if (d > kPi) d -= 2 * kPi;
			if (d < -kPi) d += 2 * kPi;
			lua_pushnumber(L, a + d * t);
			return 1;
		}
		int SIsPow2(lua_State* L) { const lua_Integer v = luaL_checkinteger(L, 1); lua_pushboolean(L, v > 0 && (v & (v - 1)) == 0); return 1; }
		int SNextPow2(lua_State* L)
		{
			lua_Integer v = luaL_checkinteger(L, 1), p = 1;
			while (p < v && p < ((lua_Integer)1 << 62)) p <<= 1;
			lua_pushinteger(L, p);
			return 1;
		}
		int SGcd(lua_State* L)
		{
			lua_Integer a = std::llabs(luaL_checkinteger(L, 1)), b = std::llabs(luaL_checkinteger(L, 2));
			while (b) { const lua_Integer t = a % b; a = b; b = t; }
			lua_pushinteger(L, a);
			return 1;
		}
		int SBinomial(lua_State* L)
		{
			const lua_Integer n = luaL_checkinteger(L, 1);
			lua_Integer k = luaL_checkinteger(L, 2);
			if (k < 0 || k > n) { lua_pushinteger(L, 0); return 1; }
			k = std::min(k, n - k);
			double r = 1;
			for (lua_Integer i = 1; i <= k; ++i)
				r = r * (double)(n - k + i) / (double)i;
			lua_pushinteger(L, (lua_Integer)std::llround(r));
			return 1;
		}
		int SBezier3(lua_State* L)
		{
			const double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2), c = luaL_checknumber(L, 3), d = luaL_checknumber(L, 4), t = luaL_checknumber(L, 5);
			const double u = 1 - t;
			lua_pushnumber(L, u * u * u * a + 3 * u * u * t * b + 3 * u * t * t * c + t * t * t * d);
			return 1;
		}
		int SCatmull(lua_State* L)
		{
			const double p0 = luaL_checknumber(L, 1), p1 = luaL_checknumber(L, 2), p2 = luaL_checknumber(L, 3), p3 = luaL_checknumber(L, 4), t = luaL_checknumber(L, 5);
			lua_pushnumber(L, 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t));
			return 1;
		}

		// ------------------------------------------------------------ easing (Penner)
		double BounceOut(double t)
		{
			const double n1 = 7.5625, d1 = 2.75;
			if (t < 1 / d1) return n1 * t * t;
			if (t < 2 / d1) { t -= 1.5 / d1; return n1 * t * t + 0.75; }
			if (t < 2.5 / d1) { t -= 2.25 / d1; return n1 * t * t + 0.9375; }
			t -= 2.625 / d1;
			return n1 * t * t + 0.984375;
		}
		using EaseFn = double (*)(double);
		struct EaseDef { const char* name; EaseFn f; };
		const double c1 = 1.70158, c2 = c1 * 1.525, c3 = c1 + 1, c4 = (2 * kPi) / 3, c5 = (2 * kPi) / 4.5;
		const EaseDef kEases[] = {
			{"linear", [](double t) { return t; }},
			{"quad_in", [](double t) { return t * t; }},
			{"quad_out", [](double t) { return 1 - (1 - t) * (1 - t); }},
			{"quad_inout", [](double t) { return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2; }},
			{"cubic_in", [](double t) { return t * t * t; }},
			{"cubic_out", [](double t) { return 1 - std::pow(1 - t, 3); }},
			{"cubic_inout", [](double t) { return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2; }},
			{"quart_in", [](double t) { return t * t * t * t; }},
			{"quart_out", [](double t) { return 1 - std::pow(1 - t, 4); }},
			{"quart_inout", [](double t) { return t < 0.5 ? 8 * t * t * t * t : 1 - std::pow(-2 * t + 2, 4) / 2; }},
			{"quint_in", [](double t) { return t * t * t * t * t; }},
			{"quint_out", [](double t) { return 1 - std::pow(1 - t, 5); }},
			{"quint_inout", [](double t) { return t < 0.5 ? 16 * t * t * t * t * t : 1 - std::pow(-2 * t + 2, 5) / 2; }},
			{"sine_in", [](double t) { return 1 - std::cos(t * kPi / 2); }},
			{"sine_out", [](double t) { return std::sin(t * kPi / 2); }},
			{"sine_inout", [](double t) { return -(std::cos(kPi * t) - 1) / 2; }},
			{"expo_in", [](double t) { return t == 0 ? 0.0 : std::pow(2, 10 * t - 10); }},
			{"expo_out", [](double t) { return t == 1 ? 1.0 : 1 - std::pow(2, -10 * t); }},
			{"expo_inout", [](double t) { return t == 0 ? 0.0 : t == 1 ? 1.0 : t < 0.5 ? std::pow(2, 20 * t - 10) / 2 : (2 - std::pow(2, -20 * t + 10)) / 2; }},
			{"circ_in", [](double t) { return 1 - std::sqrt(1 - t * t); }},
			{"circ_out", [](double t) { return std::sqrt(1 - std::pow(t - 1, 2)); }},
			{"circ_inout", [](double t) { return t < 0.5 ? (1 - std::sqrt(1 - std::pow(2 * t, 2))) / 2 : (std::sqrt(1 - std::pow(-2 * t + 2, 2)) + 1) / 2; }},
			{"back_in", [](double t) { return c3 * t * t * t - c1 * t * t; }},
			{"back_out", [](double t) { return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2); }},
			{"back_inout", [](double t) { return t < 0.5 ? (std::pow(2 * t, 2) * ((c2 + 1) * 2 * t - c2)) / 2 : (std::pow(2 * t - 2, 2) * ((c2 + 1) * (t * 2 - 2) + c2) + 2) / 2; }},
			{"elastic_in", [](double t) { return t == 0 ? 0.0 : t == 1 ? 1.0 : -std::pow(2, 10 * t - 10) * std::sin((t * 10 - 10.75) * c4); }},
			{"elastic_out", [](double t) { return t == 0 ? 0.0 : t == 1 ? 1.0 : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * c4) + 1; }},
			{"elastic_inout", [](double t) {
				 if (t == 0) return 0.0;
				 if (t == 1) return 1.0;
				 return t < 0.5 ? -(std::pow(2, 20 * t - 10) * std::sin((20 * t - 11.125) * c5)) / 2 : (std::pow(2, -20 * t + 10) * std::sin((20 * t - 11.125) * c5)) / 2 + 1; }},
			{"bounce_in", [](double t) { return 1 - BounceOut(1 - t); }},
			{"bounce_out", [](double t) { return BounceOut(t); }},
			{"bounce_inout", [](double t) { return t < 0.5 ? (1 - BounceOut(1 - 2 * t)) / 2 : (1 + BounceOut(2 * t - 1)) / 2; }},
		};

		const EaseDef* FindEase(const std::string& n)
		{
			for (const EaseDef& e : kEases)
				if (n == e.name)
					return &e;
			return nullptr;
		}
		int SEase(lua_State* L)
		{
			const std::string n = Str(L, 1);
			const EaseDef* e = FindEase(n);
			if (!e)
				return luaL_error(L, "unknown easing '%s'", n.c_str());
			lua_pushnumber(L, e->f(Clamp(luaL_checknumber(L, 2), 0, 1)));
			return 1;
		}
		int SEaseNames(lua_State* L)
		{
			lua_newtable(L);
			lua_Integer i = 1;
			for (const EaseDef& e : kEases)
			{
				lua_pushstring(L, e.name);
				lua_rawseti(L, -2, i++);
			}
			return 1;
		}
		// CSS cubic-bezier(x1,y1,x2,y2) at progress t
		int SCubicBezier(lua_State* L)
		{
			const double x1 = luaL_checknumber(L, 1), y1 = luaL_checknumber(L, 2), x2 = luaL_checknumber(L, 3), y2 = luaL_checknumber(L, 4);
			const double t = Clamp(luaL_checknumber(L, 5), 0, 1);
			auto bx = [&](double s) { const double u = 1 - s; return 3 * u * u * s * x1 + 3 * u * s * s * x2 + s * s * s; };
			auto by = [&](double s) { const double u = 1 - s; return 3 * u * u * s * y1 + 3 * u * s * s * y2 + s * s * s; };
			auto dx = [&](double s) { const double u = 1 - s; return 3 * u * u * x1 + 6 * u * s * (x2 - x1) + 3 * s * s * (1 - x2); };
			double s = t;
			for (int i = 0; i < 8; ++i)
			{
				const double err = bx(s) - t, d = dx(s);
				if (std::fabs(err) < 1e-7) break;
				if (std::fabs(d) < 1e-6) break;
				s -= err / d;
			}
			double lo = 0, hi = 1;
			if (s < 0 || s > 1 || std::fabs(bx(s) - t) > 1e-5)
			{
				s = t;
				for (int i = 0; i < 40; ++i)
				{
					const double x = bx(s);
					if (std::fabs(x - t) < 1e-7) break;
					if (x < t) lo = s; else hi = s;
					s = (lo + hi) / 2;
				}
			}
			lua_pushnumber(L, by(s));
			return 1;
		}

		// ------------------------------------------------------------ vec
		Vec* TestVec(lua_State* L, int idx)
		{
			for (int n = 2; n <= 4; ++n)
				if (void* p = luaL_testudata(L, idx, VecMeta(n)))
					return static_cast<Vec*>(p);
			return nullptr;
		}
		Vec* CheckVecSelf(lua_State* L, int idx)
		{
			Vec* v = TestVec(L, idx);
			if (!v)
				luaL_typeerror(L, idx, "vec");
			return v;
		}
		void PushLike(lua_State* L, const Vec& proto, const double r[4]) { PushVec(L, proto.n, r[0], r[1], r[2], r[3]); }

		template <int N>
		int VecNew(lua_State* L)
		{
			double d[4] = {0, 0, 0, 0};
			const int argc = lua_gettop(L);
			if (argc == 0) {}
			else if (argc == 1 && lua_isnumber(L, 1))
				for (int i = 0; i < N; ++i) d[i] = lua_tonumber(L, 1);
			else if (argc == 1)
			{
				double t[4];
				int tn = 0;
				CheckVec(L, 1, 1, t, &tn);
				for (int i = 0; i < N; ++i) d[i] = t[i];
			}
			else
			{
				int i = 0;
				for (int a = 1; a <= argc && i < N; ++a)
				{
					if (lua_isnumber(L, a))
						d[i++] = lua_tonumber(L, a);
					else
					{
						double t[4];
						int tn = 0;
						CheckVec(L, a, 1, t, &tn);
						for (int k = 0; k < tn && i < N; ++k) d[i++] = t[k];
					}
				}
			}
			PushVec(L, N, d[0], d[1], d[2], d[3]);
			return 1;
		}

		bool Operand(lua_State* L, int idx, const Vec& proto, double out[4])
		{
			if (lua_isnumber(L, idx))
			{
				for (int i = 0; i < 4; ++i) out[i] = lua_tonumber(L, idx);
				return true;
			}
			Vec* o = TestVec(L, idx);
			if (!o || o->n != proto.n)
				return false;
			for (int i = 0; i < 4; ++i) out[i] = o->v[i];
			return true;
		}
		template <int Op>
		int VecArith(lua_State* L)
		{
			// supports vec (op) vec, vec (op) number, number (op) vec
			Vec* a = TestVec(L, 1);
			Vec* b = TestVec(L, 2);
			const Vec& proto = a ? *a : *b;
			double x[4], y[4];
			if (!(a ? Operand(L, 1, proto, x) : Operand(L, 1, proto, x)) || !Operand(L, 2, proto, y))
				return luaL_error(L, "cannot combine %s with %s", luaL_typename(L, 1), luaL_typename(L, 2));
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < proto.n; ++i)
				r[i] = Op == 0 ? x[i] + y[i] : Op == 1 ? x[i] - y[i] : Op == 2 ? x[i] * y[i] : Op == 3 ? x[i] / y[i] : Op == 4 ? std::floor(x[i] / y[i]) : std::fmod(x[i], y[i]);
			PushLike(L, proto, r);
			return 1;
		}
		int VecUnm(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			double r[4] = {-a->v[0], -a->v[1], -a->v[2], -a->v[3]};
			PushLike(L, *a, r);
			return 1;
		}
		int VecEq(lua_State* L)
		{
			Vec* a = TestVec(L, 1);
			Vec* b = TestVec(L, 2);
			bool eq = a && b && a->n == b->n;
			for (int i = 0; eq && i < a->n; ++i)
				eq = a->v[i] == b->v[i];
			lua_pushboolean(L, eq);
			return 1;
		}
		int VecToString(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			std::string s = "vec" + std::to_string(a->n) + "(";
			for (int i = 0; i < a->n; ++i)
				s += (i ? ", " : "") + StrFormat("%.6g", a->v[i]);
			Push(L, s + ")");
			return 1;
		}
		int VecLen(lua_State* L) { lua_pushinteger(L, CheckVecSelf(L, 1)->n); return 1; }
		int VecIndex(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			if (lua_type(L, 2) == LUA_TSTRING)
			{
				const char* k = lua_tostring(L, 2);
				if (k[0] && !k[1])
				{
					int i = -1;
					switch (k[0])
					{
					case 'x': case 'r': i = 0; break;
					case 'y': case 'g': i = 1; break;
					case 'z': case 'b': i = 2; break;
					case 'w': case 'a': i = 3; break;
					}
					if (i >= 0)
					{
						if (i < a->n) lua_pushnumber(L, a->v[i]); else lua_pushnil(L);
						return 1;
					}
				}
				luaL_getmetatable(L, VecMeta(a->n));
				lua_getfield(L, -1, "__methods");
				lua_pushvalue(L, 2);
				lua_rawget(L, -2);
				return 1;
			}
			if (lua_isinteger(L, 2))
			{
				const lua_Integer i = lua_tointeger(L, 2);
				if (i >= 1 && i <= a->n) lua_pushnumber(L, a->v[i - 1]); else lua_pushnil(L);
				return 1;
			}
			lua_pushnil(L);
			return 1;
		}
		int VecNewIndex(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			int i = -1;
			if (lua_type(L, 2) == LUA_TSTRING)
			{
				const char* k = lua_tostring(L, 2);
				if (k[0] && !k[1])
					switch (k[0])
					{
					case 'x': case 'r': i = 0; break;
					case 'y': case 'g': i = 1; break;
					case 'z': case 'b': i = 2; break;
					case 'w': case 'a': i = 3; break;
					}
			}
			else if (lua_isinteger(L, 2))
				i = (int)lua_tointeger(L, 2) - 1;
			if (i < 0 || i >= a->n)
				return luaL_error(L, "invalid vec%d field", a->n);
			a->v[i] = luaL_checknumber(L, 3);
			return 0;
		}
		double Dot(const Vec& a, const Vec& b) { double s = 0; for (int i = 0; i < a.n; ++i) s += a.v[i] * b.v[i]; return s; }
		double LenSq(const Vec& a) { return Dot(a, a); }
		Vec* SameVec(lua_State* L, int idx, const Vec& proto)
		{
			Vec* o = TestVec(L, idx);
			if (!o || o->n != proto.n)
				luaL_argerror(L, idx, StrFormat("expected vec%d", proto.n).c_str());
			return o;
		}
		int VLength(lua_State* L) { lua_pushnumber(L, std::sqrt(LenSq(*CheckVecSelf(L, 1)))); return 1; }
		int VLengthSq(lua_State* L) { lua_pushnumber(L, LenSq(*CheckVecSelf(L, 1))); return 1; }
		int VNormalize(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			const double l = std::sqrt(LenSq(*a));
			double r[4] = {0, 0, 0, 0};
			if (l > kEps)
				for (int i = 0; i < a->n; ++i) r[i] = a->v[i] / l;
			PushLike(L, *a, r);
			return 1;
		}
		int VDot(lua_State* L) { Vec* a = CheckVecSelf(L, 1); lua_pushnumber(L, Dot(*a, *SameVec(L, 2, *a))); return 1; }
		int VCross(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			if (a->n == 2) { lua_pushnumber(L, a->v[0] * b->v[1] - a->v[1] * b->v[0]); return 1; }
			if (a->n != 3) return luaL_error(L, "cross needs vec2 or vec3");
			PushVec3(L, a->v[1] * b->v[2] - a->v[2] * b->v[1], a->v[2] * b->v[0] - a->v[0] * b->v[2], a->v[0] * b->v[1] - a->v[1] * b->v[0]);
			return 1;
		}
		int VDistance(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			double s = 0;
			for (int i = 0; i < a->n; ++i) s += (a->v[i] - b->v[i]) * (a->v[i] - b->v[i]);
			lua_pushnumber(L, std::sqrt(s));
			return 1;
		}
		int VDistanceSq(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			double s = 0;
			for (int i = 0; i < a->n; ++i) s += (a->v[i] - b->v[i]) * (a->v[i] - b->v[i]);
			lua_pushnumber(L, s);
			return 1;
		}
		int VLerp(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double t = luaL_checknumber(L, 3);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = Lerp(a->v[i], b->v[i], t);
			PushLike(L, *a, r);
			return 1;
		}
		template <int Mode>
		int VMinMax(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = Mode == 0 ? std::min(a->v[i], b->v[i]) : std::max(a->v[i], b->v[i]);
			PushLike(L, *a, r);
			return 1;
		}
		template <int Mode>
		int VMap(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i)
				r[i] = Mode == 0 ? std::fabs(a->v[i]) : Mode == 1 ? std::floor(a->v[i]) : Mode == 2 ? std::ceil(a->v[i]) : Mode == 3 ? std::floor(a->v[i] + 0.5) : (a->v[i] > 0 ? 1.0 : (a->v[i] < 0 ? -1.0 : 0.0));
			PushLike(L, *a, r);
			return 1;
		}
		int VClamp(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			double lo[4], hi[4];
			if (!Operand(L, 2, *a, lo) || !Operand(L, 3, *a, hi))
				return luaL_error(L, "clamp expects numbers or vec%d bounds", a->n);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = Clamp(a->v[i], lo[i], hi[i]);
			PushLike(L, *a, r);
			return 1;
		}
		int VClampLength(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			const double mx = luaL_checknumber(L, 2), l = std::sqrt(LenSq(*a));
			double r[4];
			const double s = l > mx && l > kEps ? mx / l : 1.0;
			for (int i = 0; i < 4; ++i) r[i] = a->v[i] * s;
			PushLike(L, *a, r);
			return 1;
		}
		int VReflect(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* nrm = SameVec(L, 2, *a);
			const double d = 2 * Dot(*a, *nrm);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = a->v[i] - d * nrm->v[i];
			PushLike(L, *a, r);
			return 1;
		}
		int VProject(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double d = LenSq(*b);
			const double s = d > kEps ? Dot(*a, *b) / d : 0;
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = b->v[i] * s;
			PushLike(L, *a, r);
			return 1;
		}
		int VAngle(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double d = std::sqrt(LenSq(*a) * LenSq(*b));
			lua_pushnumber(L, d < kEps ? 0.0 : std::acos(Clamp(Dot(*a, *b) / d, -1, 1)));
			return 1;
		}
		int VRotate(lua_State* L) // vec2 rotate by radians
		{
			Vec* a = CheckVecSelf(L, 1);
			if (a->n != 2) return luaL_error(L, "rotate is defined for vec2 (use quat * vec3)");
			const double an = luaL_checknumber(L, 2), c = std::cos(an), s = std::sin(an);
			PushVec2(L, a->v[0] * c - a->v[1] * s, a->v[0] * s + a->v[1] * c);
			return 1;
		}
		int VPerp(lua_State* L) { Vec* a = CheckVecSelf(L, 1); PushVec2(L, -a->v[1], a->v[0]); return 1; }
		int VHeading(lua_State* L) { Vec* a = CheckVecSelf(L, 1); lua_pushnumber(L, std::atan2(a->v[1], a->v[0])); return 1; }
		int VUnpack(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			for (int i = 0; i < a->n; ++i) lua_pushnumber(L, a->v[i]);
			return a->n;
		}
		int VCopy(lua_State* L) { Vec* a = CheckVecSelf(L, 1); PushLike(L, *a, a->v); return 1; }
		int VSet(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			for (int i = 0; i < a->n && 2 + i <= lua_gettop(L); ++i) a->v[i] = luaL_checknumber(L, 2 + i);
			lua_settop(L, 1);
			return 1;
		}
		int VToTable(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			lua_createtable(L, a->n, 0);
			for (int i = 0; i < a->n; ++i) { lua_pushnumber(L, a->v[i]); lua_rawseti(L, -2, i + 1); }
			return 1;
		}
		int VApprox(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double e = luaL_optnumber(L, 3, 1e-6);
			bool ok = true;
			for (int i = 0; i < a->n; ++i) ok = ok && std::fabs(a->v[i] - b->v[i]) <= e;
			lua_pushboolean(L, ok);
			return 1;
		}
		int VMoveTowards(lua_State* L)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double step = luaL_checknumber(L, 3);
			double d[4] = {0, 0, 0, 0}, l = 0;
			for (int i = 0; i < a->n; ++i) { d[i] = b->v[i] - a->v[i]; l += d[i] * d[i]; }
			l = std::sqrt(l);
			double r[4] = {0, 0, 0, 0};
			for (int i = 0; i < a->n; ++i) r[i] = (l <= step || l < kEps) ? b->v[i] : a->v[i] + d[i] / l * step;
			PushLike(L, *a, r);
			return 1;
		}
		int VSlerp(lua_State* L) // spherical interpolation for directions (vec3)
		{
			Vec* a = CheckVecSelf(L, 1);
			Vec* b = SameVec(L, 2, *a);
			const double t = luaL_checknumber(L, 3);
			const double la = std::sqrt(LenSq(*a)), lb = std::sqrt(LenSq(*b));
			if (la < kEps || lb < kEps) { lua_settop(L, 2); return VLerp(L); }
			const double d = Clamp(Dot(*a, *b) / (la * lb), -1, 1), th = std::acos(d), s = std::sin(th);
			double r[4] = {0, 0, 0, 0};
			if (s < 1e-6)
				for (int i = 0; i < a->n; ++i) r[i] = Lerp(a->v[i], b->v[i], t);
			else
			{
				const double w1 = std::sin((1 - t) * th) / s, w2 = std::sin(t * th) / s;
				for (int i = 0; i < a->n; ++i) r[i] = a->v[i] * w1 + b->v[i] * w2;
			}
			PushLike(L, *a, r);
			return 1;
		}

		template <int N>
		void RegisterVec(lua_State* L, const char* meta)
		{
			static const luaL_Reg methods[] = {{"length", VLength}, {"length_sq", VLengthSq}, {"normalize", VNormalize}, {"dot", VDot}, {"cross", VCross},
				{"distance", VDistance}, {"distance_sq", VDistanceSq}, {"lerp", VLerp}, {"min", VMinMax<0>}, {"max", VMinMax<1>}, {"abs", VMap<0>},
				{"floor", VMap<1>}, {"ceil", VMap<2>}, {"round", VMap<3>}, {"sign", VMap<4>}, {"clamp", VClamp}, {"clamp_length", VClampLength},
				{"reflect", VReflect}, {"project", VProject}, {"angle", VAngle}, {"rotate", VRotate}, {"perp", VPerp}, {"heading", VHeading},
				{"unpack", VUnpack}, {"copy", VCopy}, {"set", VSet}, {"to_table", VToTable}, {"approx", VApprox}, {"move_towards", VMoveTowards},
				{"slerp", VSlerp}, {nullptr, nullptr}};
			static const luaL_Reg mm[] = {{"__add", VecArith<0>}, {"__sub", VecArith<1>}, {"__mul", VecArith<2>}, {"__div", VecArith<3>}, {"__idiv", VecArith<4>},
				{"__mod", VecArith<5>}, {"__unm", VecUnm}, {"__eq", VecEq}, {"__tostring", VecToString}, {"__len", VecLen}, {"__index", VecIndex},
				{"__newindex", VecNewIndex}, {nullptr, nullptr}};
			luaL_newmetatable(L, meta);
			luaL_setfuncs(L, mm, 0);
			lua_newtable(L);
			luaL_setfuncs(L, methods, 0);
			lua_setfield(L, -2, "__methods");
			lua_pushstring(L, meta);
			lua_setfield(L, -2, "__name");
			lua_pop(L, 1);
		}

		// ------------------------------------------------------------ quat
		Quat* TestQ(lua_State* L, int i) { return static_cast<Quat*>(luaL_testudata(L, i, kQuat)); }
		Quat* CheckQ(lua_State* L, int i) { return static_cast<Quat*>(luaL_checkudata(L, i, kQuat)); }
		void PushQ(lua_State* L, Quat q)
		{
			Quat* p = static_cast<Quat*>(lua_newuserdatauv(L, sizeof(Quat), 0));
			*p = q;
			luaL_setmetatable(L, kQuat);
		}
		Quat QNorm(Quat q)
		{
			const double l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			if (l < kEps) return {0, 0, 0, 1};
			return {q.x / l, q.y / l, q.z / l, q.w / l};
		}
		Quat QMul(const Quat& a, const Quat& b)
		{
			return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
		}
		void QRotate(const Quat& q, const double v[3], double out[3])
		{
			// v' = v + 2w(q x v) + 2 q x (q x v)
			const double tx = 2 * (q.y * v[2] - q.z * v[1]), ty = 2 * (q.z * v[0] - q.x * v[2]), tz = 2 * (q.x * v[1] - q.y * v[0]);
			out[0] = v[0] + q.w * tx + (q.y * tz - q.z * ty);
			out[1] = v[1] + q.w * ty + (q.z * tx - q.x * tz);
			out[2] = v[2] + q.w * tz + (q.x * ty - q.y * tx);
		}
		Quat QFromAxisAngle(const double a[3], double angle)
		{
			const double l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
			if (l < kEps) return {0, 0, 0, 1};
			const double s = std::sin(angle / 2) / l;
			return {a[0] * s, a[1] * s, a[2] * s, std::cos(angle / 2)};
		}
		Quat QFromEuler(double pitch, double yaw, double roll) // q = Ry * Rx * Rz
		{
			const double z0[3] = {0, 0, 1}, x0[3] = {1, 0, 0}, y0[3] = {0, 1, 0};
			return QMul(QMul(QFromAxisAngle(y0, yaw), QFromAxisAngle(x0, pitch)), QFromAxisAngle(z0, roll));
		}
		void QToMatrix3(const Quat& q, double m[9]) // row-major 3x3
		{
			const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z, wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
			m[0] = 1 - 2 * (yy + zz); m[1] = 2 * (xy - wz); m[2] = 2 * (xz + wy);
			m[3] = 2 * (xy + wz); m[4] = 1 - 2 * (xx + zz); m[5] = 2 * (yz - wx);
			m[6] = 2 * (xz - wy); m[7] = 2 * (yz + wx); m[8] = 1 - 2 * (xx + yy);
		}
		Quat QFromMatrix3(const double m[9]) // row-major
		{
			Quat q;
			const double tr = m[0] + m[4] + m[8];
			if (tr > 0)
			{
				const double s = std::sqrt(tr + 1) * 2;
				q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25 * s};
			}
			else if (m[0] > m[4] && m[0] > m[8])
			{
				const double s = std::sqrt(1 + m[0] - m[4] - m[8]) * 2;
				q = {0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
			}
			else if (m[4] > m[8])
			{
				const double s = std::sqrt(1 + m[4] - m[0] - m[8]) * 2;
				q = {(m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
			}
			else
			{
				const double s = std::sqrt(1 + m[8] - m[0] - m[4]) * 2;
				q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s, (m[3] - m[1]) / s};
			}
			return QNorm(q);
		}
		Quat QSlerp(Quat a, Quat b, double t)
		{
			double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
			if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
			if (d > 0.9995)
				return QNorm({Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)});
			const double th = std::acos(d), s = std::sin(th), w1 = std::sin((1 - t) * th) / s, w2 = std::sin(t * th) / s;
			return {a.x * w1 + b.x * w2, a.y * w1 + b.y * w2, a.z * w1 + b.z * w2, a.w * w1 + b.w * w2};
		}

		int QNew(lua_State* L)
		{
			if (lua_gettop(L) == 0) { PushQ(L, {0, 0, 0, 1}); return 1; }
			PushQ(L, {luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_checknumber(L, 4)});
			return 1;
		}
		int QEuler(lua_State* L) { PushQ(L, QFromEuler(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_optnumber(L, 3, 0))); return 1; }
		int QAxisAngle(lua_State* L)
		{
			double a[4];
			CheckVec(L, 1, 3, a);
			PushQ(L, QFromAxisAngle(a, luaL_checknumber(L, 2)));
			return 1;
		}
		int QFromTo(lua_State* L)
		{
			double a[4], b[4];
			CheckVec(L, 1, 3, a);
			CheckVec(L, 2, 3, b);
			auto nrm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > kEps) { v[0] /= l; v[1] /= l; v[2] /= l; } };
			nrm(a); nrm(b);
			const double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
			if (d > 0.999999) { PushQ(L, {0, 0, 0, 1}); return 1; }
			if (d < -0.999999)
			{
				double ax[3] = {1, 0, 0};
				if (std::fabs(a[0]) > 0.9) { ax[0] = 0; ax[1] = 1; }
				const double c[3] = {a[1] * ax[2] - a[2] * ax[1], a[2] * ax[0] - a[0] * ax[2], a[0] * ax[1] - a[1] * ax[0]};
				PushQ(L, QFromAxisAngle(c, kPi));
				return 1;
			}
			PushQ(L, QNorm({a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0], 1 + d}));
			return 1;
		}
		// maps +Z to `forward`, with +Y as close to `up` as possible
		int QLook(lua_State* L)
		{
			double f[4], u[4] = {0, 1, 0, 0};
			CheckVec(L, 1, 3, f);
			if (!lua_isnoneornil(L, 2)) CheckVec(L, 2, 3, u);
			auto nrm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > kEps) { v[0] /= l; v[1] /= l; v[2] /= l; } return l; };
			if (nrm(f) < kEps) { PushQ(L, {0, 0, 0, 1}); return 1; }
			nrm(u);
			double x[3] = {u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0]};
			if (nrm(x) < 1e-6)
			{
				const double alt[3] = {1, 0, 0};
				x[0] = alt[1] * f[2] - alt[2] * f[1]; x[1] = alt[2] * f[0] - alt[0] * f[2]; x[2] = alt[0] * f[1] - alt[1] * f[0];
				nrm(x);
			}
			const double y[3] = {f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0]};
			const double m[9] = {x[0], y[0], f[0], x[1], y[1], f[1], x[2], y[2], f[2]};
			PushQ(L, QFromMatrix3(m));
			return 1;
		}
		int QMulMeta(lua_State* L)
		{
			Quat* a = TestQ(L, 1);
			if (a)
			{
				if (Quat* b = TestQ(L, 2)) { PushQ(L, QNorm(QMul(*a, *b))); return 1; }
				double v[4];
				int n = 3;
				if (ToVec(L, 2, 3, v, &n))
				{
					double o[3];
					QRotate(*a, v, o);
					PushVec3(L, o[0], o[1], o[2]);
					return 1;
				}
			}
			return luaL_error(L, "quat can be multiplied by quat or vec3");
		}
		int QEq(lua_State* L)
		{
			Quat* a = TestQ(L, 1);
			Quat* b = TestQ(L, 2);
			lua_pushboolean(L, a && b && a->x == b->x && a->y == b->y && a->z == b->z && a->w == b->w);
			return 1;
		}
		int QToString(lua_State* L) { Quat* q = CheckQ(L, 1); Push(L, StrFormat("quat(%.6g, %.6g, %.6g, %.6g)", q->x, q->y, q->z, q->w)); return 1; }
		int QIndex(lua_State* L)
		{
			Quat* q = CheckQ(L, 1);
			if (lua_type(L, 2) == LUA_TSTRING)
			{
				const char* k = lua_tostring(L, 2);
				if (!strcmp(k, "x")) { lua_pushnumber(L, q->x); return 1; }
				if (!strcmp(k, "y")) { lua_pushnumber(L, q->y); return 1; }
				if (!strcmp(k, "z")) { lua_pushnumber(L, q->z); return 1; }
				if (!strcmp(k, "w")) { lua_pushnumber(L, q->w); return 1; }
				luaL_getmetatable(L, kQuat);
				lua_getfield(L, -1, "__methods");
				lua_pushvalue(L, 2);
				lua_rawget(L, -2);
				return 1;
			}
			lua_pushnil(L);
			return 1;
		}
		int QMNormalize(lua_State* L) { PushQ(L, QNorm(*CheckQ(L, 1))); return 1; }
		int QMInverse(lua_State* L) { Quat q = QNorm(*CheckQ(L, 1)); PushQ(L, {-q.x, -q.y, -q.z, q.w}); return 1; }
		int QMDot(lua_State* L) { Quat* a = CheckQ(L, 1); Quat* b = CheckQ(L, 2); lua_pushnumber(L, a->x * b->x + a->y * b->y + a->z * b->z + a->w * b->w); return 1; }
		int QMSlerp(lua_State* L) { PushQ(L, QSlerp(*CheckQ(L, 1), *CheckQ(L, 2), luaL_checknumber(L, 3))); return 1; }
		int QMNlerp(lua_State* L)
		{
			Quat a = *CheckQ(L, 1), b = *CheckQ(L, 2);
			const double t = luaL_checknumber(L, 3);
			if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0) b = {-b.x, -b.y, -b.z, -b.w};
			PushQ(L, QNorm({Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)}));
			return 1;
		}
		int QMRotate(lua_State* L)
		{
			Quat* q = CheckQ(L, 1);
			double v[4];
			CheckVec(L, 2, 3, v);
			double o[3];
			QRotate(*q, v, o);
			PushVec3(L, o[0], o[1], o[2]);
			return 1;
		}
		int QMEuler(lua_State* L)
		{
			double m[9];
			QToMatrix3(QNorm(*CheckQ(L, 1)), m);
			double pitch, yaw, roll;
			const double sp = Clamp(-m[5], -1, 1);
			pitch = std::asin(sp);
			if (std::fabs(sp) < 0.9999)
			{
				yaw = std::atan2(m[2], m[8]);
				roll = std::atan2(m[3], m[4]);
			}
			else
			{
				yaw = std::atan2(-m[6], m[0]);
				roll = 0;
			}
			PushVec3(L, pitch, yaw, roll);
			return 1;
		}
		int QMAxisAngle(lua_State* L)
		{
			Quat q = QNorm(*CheckQ(L, 1));
			if (q.w < 0) q = {-q.x, -q.y, -q.z, -q.w};
			const double s = std::sqrt(std::max(0.0, 1 - q.w * q.w));
			if (s < 1e-9) PushVec3(L, 1, 0, 0); else PushVec3(L, q.x / s, q.y / s, q.z / s);
			lua_pushnumber(L, 2 * std::acos(Clamp(q.w, -1, 1)));
			return 2;
		}
		int QMAngle(lua_State* L)
		{
			Quat a = QNorm(*CheckQ(L, 1)), b = QNorm(*CheckQ(L, 2));
			lua_pushnumber(L, 2 * std::acos(Clamp(std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 0, 1)));
			return 1;
		}
		int QMUnpack(lua_State* L) { Quat* q = CheckQ(L, 1); lua_pushnumber(L, q->x); lua_pushnumber(L, q->y); lua_pushnumber(L, q->z); lua_pushnumber(L, q->w); return 4; }
		int QMToMatrix(lua_State* L);

		// ------------------------------------------------------------ mat4
		Mat4* TestM(lua_State* L, int i) { return static_cast<Mat4*>(luaL_testudata(L, i, kMat4)); }
		Mat4* CheckM(lua_State* L, int i) { return static_cast<Mat4*>(luaL_checkudata(L, i, kMat4)); }
		Mat4* NewM(lua_State* L)
		{
			Mat4* m = static_cast<Mat4*>(lua_newuserdatauv(L, sizeof(Mat4), 0));
			memset(m->m, 0, sizeof(m->m));
			luaL_setmetatable(L, kMat4);
			return m;
		}
		void MIdentity(Mat4& m) { memset(m.m, 0, sizeof(m.m)); m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1; }
		Mat4 MMul(const Mat4& a, const Mat4& b)
		{
			Mat4 r;
			for (int c = 0; c < 4; ++c)
				for (int rr = 0; rr < 4; ++rr)
				{
					double s = 0;
					for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
					r.m[c * 4 + rr] = s;
				}
			return r;
		}
		bool MInverse(const Mat4& s, Mat4& out)
		{
			const double* m = s.m;
			double inv[16];
			inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
			inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
			inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
			inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
			inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
			inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
			inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
			inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
			inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
			inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
			inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
			inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
			inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
			const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
			if (std::fabs(det) < 1e-300)
				return false;
			for (int i = 0; i < 16; ++i)
				out.m[i] = inv[i] / det;
			return true;
		}
		double MDet(const Mat4& s)
		{
			const double* m = s.m;
			const double i0 = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			const double i4 = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			const double i8 = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			const double i12 = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
			return m[0] * i0 + m[1] * i4 + m[2] * i8 + m[3] * i12;
		}
		Mat4 MTRS(const double t[3], const Quat& q, const double s[3])
		{
			double r[9];
			QToMatrix3(q, r);
			Mat4 m;
			MIdentity(m);
			for (int row = 0; row < 3; ++row)
				for (int col = 0; col < 3; ++col)
					m.m[col * 4 + row] = r[row * 3 + col] * s[col];
			m.m[12] = t[0]; m.m[13] = t[1]; m.m[14] = t[2];
			return m;
		}

		int MIdent(lua_State* L) { MIdentity(*NewM(L)); return 1; }
		int MTranslation(lua_State* L)
		{
			double v[4];
			CheckVec(L, 1, 3, v);
			Mat4* m = NewM(L);
			MIdentity(*m);
			m->m[12] = v[0]; m->m[13] = v[1]; m->m[14] = v[2];
			return 1;
		}
		int MScale(lua_State* L)
		{
			double v[4];
			if (lua_isnumber(L, 1)) v[0] = v[1] = v[2] = lua_tonumber(L, 1); else CheckVec(L, 1, 3, v);
			Mat4* m = NewM(L);
			MIdentity(*m);
			m->m[0] = v[0]; m->m[5] = v[1]; m->m[10] = v[2];
			return 1;
		}
		int MRotation(lua_State* L)
		{
			const Mat4 r = MTRS(std::array<double, 3>{0, 0, 0}.data(), QNorm(*CheckQ(L, 1)), std::array<double, 3>{1, 1, 1}.data());
			*NewM(L) = r;
			return 1;
		}
		int MTrs(lua_State* L)
		{
			double t[4], s[4] = {1, 1, 1, 0};
			CheckVec(L, 1, 3, t);
			Quat q = {0, 0, 0, 1};
			if (!lua_isnoneornil(L, 2)) q = QNorm(*CheckQ(L, 2));
			if (!lua_isnoneornil(L, 3)) { if (lua_isnumber(L, 3)) s[0] = s[1] = s[2] = lua_tonumber(L, 3); else CheckVec(L, 3, 3, s); }
			*NewM(L) = MTRS(t, q, s);
			return 1;
		}
		bool LeftHanded(lua_State* L, int idx) { return lua_isnoneornil(L, idx) || std::string(lua_tostring(L, idx)) != "rh"; }
		int MPerspective(lua_State* L)
		{
			const double fovy = luaL_checknumber(L, 1), aspect = luaL_checknumber(L, 2), n = luaL_checknumber(L, 3), f = luaL_checknumber(L, 4);
			const bool lh = LeftHanded(L, 5);
			const double t = 1.0 / std::tan(fovy / 2);
			Mat4* m = NewM(L);
			m->m[0] = t / aspect;
			m->m[5] = t;
			if (lh) { m->m[10] = f / (f - n); m->m[11] = 1; m->m[14] = -n * f / (f - n); }
			else { m->m[10] = f / (n - f); m->m[11] = -1; m->m[14] = n * f / (n - f); }
			return 1;
		}
		int MOrtho(lua_State* L)
		{
			const double l = luaL_checknumber(L, 1), r = luaL_checknumber(L, 2), b = luaL_checknumber(L, 3), t = luaL_checknumber(L, 4), n = luaL_checknumber(L, 5), f = luaL_checknumber(L, 6);
			const bool lh = LeftHanded(L, 7);
			Mat4* m = NewM(L);
			MIdentity(*m);
			m->m[0] = 2 / (r - l); m->m[5] = 2 / (t - b);
			m->m[12] = -(r + l) / (r - l); m->m[13] = -(t + b) / (t - b);
			m->m[10] = (lh ? 1 : -1) / (f - n);
			m->m[14] = -n / (f - n);
			return 1;
		}
		int MLookAt(lua_State* L)
		{
			double e[4], tg[4], u[4] = {0, 1, 0, 0};
			CheckVec(L, 1, 3, e);
			CheckVec(L, 2, 3, tg);
			if (!lua_isnoneornil(L, 3)) CheckVec(L, 3, 3, u);
			const bool lh = LeftHanded(L, 4);
			double z[3] = {lh ? tg[0] - e[0] : e[0] - tg[0], lh ? tg[1] - e[1] : e[1] - tg[1], lh ? tg[2] - e[2] : e[2] - tg[2]};
			auto nrm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > kEps) { v[0] /= l; v[1] /= l; v[2] /= l; } };
			nrm(z);
			double x[3] = {u[1] * z[2] - u[2] * z[1], u[2] * z[0] - u[0] * z[2], u[0] * z[1] - u[1] * z[0]};
			nrm(x);
			const double y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
			Mat4* m = NewM(L);
			MIdentity(*m);
			const double* rows[3] = {x, y, z};
			for (int r = 0; r < 3; ++r)
			{
				for (int c = 0; c < 3; ++c) m->m[c * 4 + r] = rows[r][c];
				m->m[12 + r] = -(rows[r][0] * e[0] + rows[r][1] * e[1] + rows[r][2] * e[2]);
			}
			return 1;
		}
		int MFromTable(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			Mat4* m = NewM(L);
			for (int i = 0; i < 16; ++i)
			{
				lua_rawgeti(L, 1, i + 1);
				m->m[i] = luaL_checknumber(L, -1);
				lua_pop(L, 1);
			}
			return 1;
		}
		int MMulMeta(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			if (Mat4* b = TestM(L, 2)) { *NewM(L) = MMul(*a, *b); return 1; }
			double v[4];
			int n = 0;
			if (ToVec(L, 2, 3, v, &n))
			{
				if (n == 4)
				{
					double r[4];
					for (int row = 0; row < 4; ++row) r[row] = a->m[row] * v[0] + a->m[4 + row] * v[1] + a->m[8 + row] * v[2] + a->m[12 + row] * v[3];
					PushVec(L, 4, r[0], r[1], r[2], r[3]);
				}
				else
				{
					double r[4];
					for (int row = 0; row < 4; ++row) r[row] = a->m[row] * v[0] + a->m[4 + row] * v[1] + a->m[8 + row] * v[2] + a->m[12 + row];
					const double w = std::fabs(r[3]) > kEps ? r[3] : 1.0;
					PushVec3(L, r[0] / w, r[1] / w, r[2] / w);
				}
				return 1;
			}
			return luaL_error(L, "mat4 can be multiplied by mat4, vec3 (point) or vec4");
		}
		int MEq(lua_State* L)
		{
			Mat4* a = TestM(L, 1);
			Mat4* b = TestM(L, 2);
			lua_pushboolean(L, a && b && !memcmp(a->m, b->m, sizeof(a->m)));
			return 1;
		}
		int MToString(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			std::string s = "mat4(";
			for (int r = 0; r < 4; ++r)
			{
				s += r ? "; " : "";
				for (int c = 0; c < 4; ++c) s += (c ? ", " : "") + StrFormat("%.5g", a->m[c * 4 + r]);
			}
			Push(L, s + ")");
			return 1;
		}
		int MMInverse(lua_State* L)
		{
			Mat4 r;
			if (!MInverse(*CheckM(L, 1), r))
				return Fail(L, "matrix is singular");
			*NewM(L) = r;
			return 1;
		}
		int MMTranspose(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			Mat4 r;
			for (int c = 0; c < 4; ++c)
				for (int rr = 0; rr < 4; ++rr) r.m[c * 4 + rr] = a->m[rr * 4 + c];
			*NewM(L) = r;
			return 1;
		}
		int MMDet(lua_State* L) { lua_pushnumber(L, MDet(*CheckM(L, 1))); return 1; }
		int MMPoint(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			double v[4];
			CheckVec(L, 2, 3, v);
			double r[4];
			for (int row = 0; row < 4; ++row) r[row] = a->m[row] * v[0] + a->m[4 + row] * v[1] + a->m[8 + row] * v[2] + a->m[12 + row];
			const double w = std::fabs(r[3]) > kEps ? r[3] : 1.0;
			PushVec3(L, r[0] / w, r[1] / w, r[2] / w);
			return 1;
		}
		int MMDir(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			double v[4];
			CheckVec(L, 2, 3, v);
			double r[3];
			for (int row = 0; row < 3; ++row) r[row] = a->m[row] * v[0] + a->m[4 + row] * v[1] + a->m[8 + row] * v[2];
			PushVec3(L, r[0], r[1], r[2]);
			return 1;
		}
		int MMDecompose(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			double sx = std::sqrt(a->m[0] * a->m[0] + a->m[1] * a->m[1] + a->m[2] * a->m[2]);
			double sy = std::sqrt(a->m[4] * a->m[4] + a->m[5] * a->m[5] + a->m[6] * a->m[6]);
			double sz = std::sqrt(a->m[8] * a->m[8] + a->m[9] * a->m[9] + a->m[10] * a->m[10]);
			if (MDet(*a) < 0) sx = -sx;
			double r[9] = {a->m[0] / sx, a->m[4] / sy, a->m[8] / sz, a->m[1] / sx, a->m[5] / sy, a->m[9] / sz, a->m[2] / sx, a->m[6] / sy, a->m[10] / sz};
			PushVec3(L, a->m[12], a->m[13], a->m[14]);
			PushQ(L, QFromMatrix3(r));
			PushVec3(L, sx, sy, sz);
			return 3;
		}
		int MMGet(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			const lua_Integer r = luaL_checkinteger(L, 2), c = luaL_checkinteger(L, 3);
			luaL_argcheck(L, r >= 1 && r <= 4, 2, "row 1..4");
			luaL_argcheck(L, c >= 1 && c <= 4, 3, "column 1..4");
			lua_pushnumber(L, a->m[(c - 1) * 4 + (r - 1)]);
			return 1;
		}
		int MMSet(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			const lua_Integer r = luaL_checkinteger(L, 2), c = luaL_checkinteger(L, 3);
			luaL_argcheck(L, r >= 1 && r <= 4, 2, "row 1..4");
			luaL_argcheck(L, c >= 1 && c <= 4, 3, "column 1..4");
			a->m[(c - 1) * 4 + (r - 1)] = luaL_checknumber(L, 4);
			return 0;
		}
		int MMToTable(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			lua_createtable(L, 16, 0);
			for (int i = 0; i < 16; ++i) { lua_pushnumber(L, a->m[i]); lua_rawseti(L, -2, i + 1); }
			return 1;
		}
		int MMCopy(lua_State* L) { *NewM(L) = *CheckM(L, 1); return 1; }
		int QMToMatrix(lua_State* L)
		{
			const double z[3] = {0, 0, 0}, o[3] = {1, 1, 1};
			*NewM(L) = MTRS(z, QNorm(*CheckQ(L, 1)), o);
			return 1;
		}
		// quat from the rotation part of a mat4
		int QFromMat(lua_State* L)
		{
			Mat4* a = CheckM(L, 1);
			double sx = std::sqrt(a->m[0] * a->m[0] + a->m[1] * a->m[1] + a->m[2] * a->m[2]);
			double sy = std::sqrt(a->m[4] * a->m[4] + a->m[5] * a->m[5] + a->m[6] * a->m[6]);
			double sz = std::sqrt(a->m[8] * a->m[8] + a->m[9] * a->m[9] + a->m[10] * a->m[10]);
			if (sx < kEps || sy < kEps || sz < kEps) { PushQ(L, {0, 0, 0, 1}); return 1; }
			const double r[9] = {a->m[0] / sx, a->m[4] / sy, a->m[8] / sz, a->m[1] / sx, a->m[5] / sy, a->m[9] / sz, a->m[2] / sx, a->m[6] / sy, a->m[10] / sz};
			PushQ(L, QFromMatrix3(r));
			return 1;
		}
	} // namespace

	int OpenMathx(lua_State* L)
	{
		RegisterVec<2>(L, kVec2);
		RegisterVec<3>(L, kVec3);
		RegisterVec<4>(L, kVec4);

		static const luaL_Reg qm[] = {{"normalize", QMNormalize}, {"inverse", QMInverse}, {"dot", QMDot}, {"slerp", QMSlerp}, {"nlerp", QMNlerp},
			{"rotate", QMRotate}, {"to_euler", QMEuler}, {"to_axis_angle", QMAxisAngle}, {"angle_to", QMAngle}, {"unpack", QMUnpack},
			{"to_mat4", QMToMatrix}, {nullptr, nullptr}};
		static const luaL_Reg qmm[] = {{"__mul", QMulMeta}, {"__eq", QEq}, {"__tostring", QToString}, {"__index", QIndex}, {nullptr, nullptr}};
		luaL_newmetatable(L, kQuat);
		luaL_setfuncs(L, qmm, 0);
		lua_newtable(L);
		luaL_setfuncs(L, qm, 0);
		lua_setfield(L, -2, "__methods");
		lua_pop(L, 1);

		static const luaL_Reg mm[] = {{"inverse", MMInverse}, {"transpose", MMTranspose}, {"determinant", MMDet}, {"transform_point", MMPoint},
			{"transform_dir", MMDir}, {"decompose", MMDecompose}, {"get", MMGet}, {"set", MMSet}, {"to_table", MMToTable}, {"copy", MMCopy}, {nullptr, nullptr}};
		static const luaL_Reg mmm[] = {{"__mul", MMulMeta}, {"__eq", MEq}, {"__tostring", MToString}, {nullptr, nullptr}};
		luaL_newmetatable(L, kMat4);
		luaL_setfuncs(L, mmm, 0);
		lua_newtable(L);
		luaL_setfuncs(L, mm, 0);
		lua_setfield(L, -2, "__index");
		lua_pop(L, 1);

		static const luaL_Reg fns[] = {{"lerp", SLerp}, {"inverse_lerp", SInvLerp}, {"remap", SRemap}, {"clamp", SClamp}, {"saturate", SSaturate},
			{"smoothstep", SSmoothstep}, {"smootherstep", SSmootherstep}, {"wrap", SWrap}, {"pingpong", SPingPong}, {"sign", SSign},
			{"approach", SApproach}, {"damp", SDamp}, {"smooth_damp", SSmoothDamp}, {"spring", SSpring}, {"deg", SDeg}, {"rad", SRad},
			{"round", SRound}, {"approx", SApprox}, {"fract", SFract}, {"angle_diff", SAngleDiff}, {"lerp_angle", SLerpAngle}, {"is_pow2", SIsPow2},
			{"next_pow2", SNextPow2}, {"gcd", SGcd}, {"binomial", SBinomial}, {"bezier3", SBezier3}, {"catmull_rom", SCatmull}, {"ease", SEase},
			{"ease_names", SEaseNames}, {"cubic_bezier", SCubicBezier}, {"vec2", VecNew<2>}, {"vec3", VecNew<3>}, {"vec4", VecNew<4>},
			{"quat", QNew}, {"quat_euler", QEuler}, {"quat_axis_angle", QAxisAngle}, {"quat_from_to", QFromTo}, {"quat_look", QLook},
			{"quat_from_mat4", QFromMat}, {"mat4", MIdent}, {"mat4_translation", MTranslation}, {"mat4_scale", MScale}, {"mat4_rotation", MRotation},
			{"mat4_trs", MTrs}, {"mat4_perspective", MPerspective}, {"mat4_ortho", MOrtho}, {"mat4_look_at", MLookAt}, {"mat4_from_table", MFromTable},
			{nullptr, nullptr}};
		luaL_newlib(L, fns);
		lua_pushnumber(L, kPi);
		lua_setfield(L, -2, "pi");
		lua_pushnumber(L, kPi * 2);
		lua_setfield(L, -2, "tau");
		lua_pushnumber(L, 1e-6);
		lua_setfield(L, -2, "epsilon");
		return 1;
	}
} // namespace lumen::lua::detail
