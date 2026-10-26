// lumen.random, lumen.noise, lumen.geom, lumen.color, lumen.utf8x
#include "LuaVec.h"
#include "lumen/core/Strings.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>
#include <random>

namespace lumen::lua::detail
{
	namespace
	{
		const double kPi = 3.14159265358979323846;
		double Clamp(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

		// ============================================================== random (PCG32)
		struct Pcg
		{
			uint64_t state = 0, inc = 1;
			bool haveSpare = false;
			double spare = 0;
			void Seed(uint64_t initState, uint64_t initSeq)
			{
				state = 0;
				inc = (initSeq << 1u) | 1u;
				Next();
				state += initState;
				Next();
				haveSpare = false;
			}
			uint32_t Next()
			{
				const uint64_t old = state;
				state = old * 6364136223846793005ULL + inc;
				const uint32_t xs = (uint32_t)(((old >> 18u) ^ old) >> 27u);
				const uint32_t rot = (uint32_t)(old >> 59u);
				return (xs >> rot) | (xs << ((~rot + 1u) & 31));
			}
			double Float() // [0,1) with 53 bits
			{
				const uint64_t a = Next() >> 5, b = Next() >> 6;
				return ((double)a * 67108864.0 + (double)b) / 9007199254740992.0;
			}
			uint64_t Below(uint64_t n) // unbiased [0,n)
			{
				if (n <= 1) return 0;
				if (n <= 0xFFFFFFFFull)
				{
					const uint32_t nn = (uint32_t)n;
					const uint32_t threshold = (uint32_t)(-nn) % nn;
					for (;;)
					{
						const uint32_t r = Next();
						if (r >= threshold) return r % nn;
					}
				}
				const uint64_t threshold = (uint64_t)(-(int64_t)n) % n;
				for (;;)
				{
					const uint64_t r = ((uint64_t)Next() << 32) | Next();
					if (r >= threshold) return r % n;
				}
			}
			double Normal()
			{
				if (haveSpare) { haveSpare = false; return spare; }
				double u, v, s;
				do
				{
					u = Float() * 2 - 1;
					v = Float() * 2 - 1;
					s = u * u + v * v;
				} while (s >= 1 || s == 0);
				const double m = std::sqrt(-2 * std::log(s) / s);
				spare = v * m;
				haveSpare = true;
				return u * m;
			}
		};

		const char* kRng = "lumen.rng";

		// Module level functions get the shared generator as upvalue 1 and arguments start at 1;
		// methods get the generator as argument 1 and arguments start at 2.
		template <bool Method>
		Pcg* G(lua_State* L, int& base)
		{
			if (Method)
			{
				base = 2;
				return static_cast<Pcg*>(luaL_checkudata(L, 1, kRng));
			}
			base = 1;
			return static_cast<Pcg*>(lua_touserdata(L, lua_upvalueindex(1)));
		}

		template <bool M> int RInt(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			lua_Integer lo = 1, hi = 0;
			if (lua_gettop(L) < b) { lua_pushinteger(L, (lua_Integer)(g->Next() >> 1)); return 1; }
			if (lua_gettop(L) == b) { lo = 1; hi = luaL_checkinteger(L, b); }
			else { lo = luaL_checkinteger(L, b); hi = luaL_checkinteger(L, b + 1); }
			if (hi < lo) std::swap(lo, hi);
			const uint64_t span = (uint64_t)(hi - lo) + 1;
			lua_pushinteger(L, lo + (lua_Integer)(span == 0 ? ((uint64_t)g->Next() << 32 | g->Next()) : g->Below(span)));
			return 1;
		}
		template <bool M> int RFloat(lua_State* L) { int b; Pcg* g = G<M>(L, b); lua_pushnumber(L, g->Float()); return 1; }
		template <bool M> int RRange(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const double lo = luaL_checknumber(L, b), hi = luaL_checknumber(L, b + 1);
			lua_pushnumber(L, lo + (hi - lo) * g->Float());
			return 1;
		}
		template <bool M> int RNormal(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			lua_pushnumber(L, luaL_optnumber(L, b, 0) + luaL_optnumber(L, b + 1, 1) * g->Normal());
			return 1;
		}
		template <bool M> int RBool(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			lua_pushboolean(L, g->Float() < luaL_optnumber(L, b, 0.5));
			return 1;
		}
		template <bool M> int RPick(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			luaL_checktype(L, b, LUA_TTABLE);
			const lua_Integer n = (lua_Integer)luaL_len(L, b);
			if (n == 0) { lua_pushnil(L); return 1; }
			lua_rawgeti(L, b, 1 + (lua_Integer)g->Below((uint64_t)n));
			return 1;
		}
		template <bool M> int RShuffle(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			luaL_checktype(L, b, LUA_TTABLE);
			const lua_Integer n = (lua_Integer)luaL_len(L, b);
			for (lua_Integer i = n; i > 1; --i)
			{
				const lua_Integer j = 1 + (lua_Integer)g->Below((uint64_t)i);
				lua_rawgeti(L, b, i);
				lua_rawgeti(L, b, j);
				lua_rawseti(L, b, i);
				lua_rawseti(L, b, j);
			}
			lua_pushvalue(L, b);
			return 1;
		}
		// weighted(weights) -> index (1-based); weights are non-negative numbers
		template <bool M> int RWeighted(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			luaL_checktype(L, b, LUA_TTABLE);
			const lua_Integer n = (lua_Integer)luaL_len(L, b);
			double total = 0;
			std::vector<double> w((size_t)n);
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, b, i);
				w[(size_t)i - 1] = std::max(0.0, luaL_checknumber(L, -1));
				total += w[(size_t)i - 1];
				lua_pop(L, 1);
			}
			if (total <= 0) { lua_pushnil(L); return 1; }
			double r = g->Float() * total;
			for (lua_Integer i = 0; i < n; ++i)
			{
				r -= w[(size_t)i];
				if (r < 0) { lua_pushinteger(L, i + 1); return 1; }
			}
			lua_pushinteger(L, n);
			return 1;
		}
		// sample(t, k): k distinct elements
		template <bool M> int RSample(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			luaL_checktype(L, b, LUA_TTABLE);
			const lua_Integer n = (lua_Integer)luaL_len(L, b);
			const lua_Integer k = std::min(n, luaL_checkinteger(L, b + 1));
			std::vector<lua_Integer> idx((size_t)n);
			for (lua_Integer i = 0; i < n; ++i) idx[(size_t)i] = i + 1;
			lua_newtable(L);
			for (lua_Integer i = 0; i < k; ++i)
			{
				const size_t j = (size_t)i + (size_t)g->Below((uint64_t)(n - i));
				std::swap(idx[(size_t)i], idx[j]);
				lua_rawgeti(L, b, idx[(size_t)i]);
				lua_rawseti(L, -2, i + 1);
			}
			return 1;
		}
		template <bool M> int RUnit2(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const double a = g->Float() * 2 * kPi;
			PushVec2(L, std::cos(a), std::sin(a));
			return 1;
		}
		template <bool M> int RUnit3(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const double z = g->Float() * 2 - 1, a = g->Float() * 2 * kPi, r = std::sqrt(1 - z * z);
			PushVec3(L, r * std::cos(a), r * std::sin(a), z);
			return 1;
		}
		template <bool M> int RInCircle(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const double r = luaL_optnumber(L, b, 1) * std::sqrt(g->Float()), a = g->Float() * 2 * kPi;
			PushVec2(L, r * std::cos(a), r * std::sin(a));
			return 1;
		}
		template <bool M> int RInSphere(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const double rr = luaL_optnumber(L, b, 1) * std::cbrt(g->Float());
			const double z = g->Float() * 2 - 1, a = g->Float() * 2 * kPi, r = std::sqrt(1 - z * z);
			PushVec3(L, rr * r * std::cos(a), rr * r * std::sin(a), rr * z);
			return 1;
		}
		// dice("2d6+3") -> total, {rolls}
		template <bool M> int RDice(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const std::string s = Str(L, b);
			int n = 1, sides = 0, mod = 0;
			char sign = '+';
			int got = sscanf(s.c_str(), "%dd%d%c%d", &n, &sides, &sign, &mod);
			if (got < 2 || n < 1 || n > 1000 || sides < 1)
				return luaL_error(L, "bad dice expression '%s' (use NdM or NdM+K)", s.c_str());
			if (got < 4) mod = 0;
			if (sign == '-') mod = -mod;
			lua_newtable(L);
			lua_Integer total = mod;
			for (int i = 1; i <= n; ++i)
			{
				const lua_Integer r = 1 + (lua_Integer)g->Below((uint64_t)sides);
				total += r;
				lua_pushinteger(L, r);
				lua_rawseti(L, -2, i);
			}
			lua_pushinteger(L, total);
			lua_insert(L, -2);
			return 2;
		}
		template <bool M> int RBytes(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			const lua_Integer n = luaL_checkinteger(L, b);
			luaL_argcheck(L, n >= 0 && n <= (1 << 24), b, "size out of range");
			std::string out((size_t)n, 0);
			for (auto& c : out) c = (char)(g->Next() & 0xff);
			Push(L, out);
			return 1;
		}
		template <bool M> int RSeed(lua_State* L)
		{
			int b;
			Pcg* g = G<M>(L, b);
			g->Seed((uint64_t)luaL_checkinteger(L, b), (uint64_t)luaL_optinteger(L, b + 1, 54));
			return 0;
		}
		int RState(lua_State* L)
		{
			Pcg* g = static_cast<Pcg*>(luaL_checkudata(L, 1, kRng));
			lua_pushinteger(L, (lua_Integer)g->state);
			lua_pushinteger(L, (lua_Integer)g->inc);
			return 2;
		}
		int RSetState(lua_State* L)
		{
			Pcg* g = static_cast<Pcg*>(luaL_checkudata(L, 1, kRng));
			g->state = (uint64_t)luaL_checkinteger(L, 2);
			g->inc = (uint64_t)luaL_checkinteger(L, 3) | 1u;
			g->haveSpare = false;
			return 0;
		}
		int RClone(lua_State* L)
		{
			Pcg* g = static_cast<Pcg*>(luaL_checkudata(L, 1, kRng));
			Pcg* c = NewUdata<Pcg>(L, kRng);
			*c = *g;
			return 1;
		}
		int RNew(lua_State* L)
		{
			const bool haveSeed = !lua_isnoneornil(L, 1);
			const uint64_t seed = haveSeed ? (uint64_t)luaL_checkinteger(L, 1) : 0, stream = (uint64_t)luaL_optinteger(L, 2, 54);
			Pcg* g = NewUdata<Pcg>(L, kRng);
			if (!haveSeed)
			{
				std::random_device rd;
				g->Seed(((uint64_t)rd() << 32) | rd(), ((uint64_t)rd() << 32) | rd());
			}
			else
				g->Seed(seed, stream);
			return 1;
		}
		uint32_t HashMix(uint32_t h)
		{
			h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15; h *= 0x846ca68bU; h ^= h >> 16;
			return h;
		}
		// random.hash(seed, a, b, c, ...) -> float in [0,1); stable across platforms
		int RHash(lua_State* L)
		{
			uint32_t h = 0x9e3779b9u;
			for (int i = 1; i <= lua_gettop(L); ++i)
				h = HashMix(h ^ (uint32_t)(luaL_checkinteger(L, i) * 0x85ebca6bLL) ^ (h << 6) ^ (h >> 2));
			lua_pushnumber(L, (double)(h >> 8) / 16777216.0);
			return 1;
		}
	} // namespace

	int OpenRandom(lua_State* L)
	{
		static const luaL_Reg rm[] = {{"int", RInt<true>}, {"float", RFloat<true>}, {"range", RRange<true>}, {"normal", RNormal<true>}, {"bool", RBool<true>},
			{"pick", RPick<true>}, {"shuffle", RShuffle<true>}, {"weighted", RWeighted<true>}, {"sample", RSample<true>}, {"unit2", RUnit2<true>},
			{"unit3", RUnit3<true>}, {"in_circle", RInCircle<true>}, {"in_sphere", RInSphere<true>}, {"dice", RDice<true>}, {"bytes", RBytes<true>},
			{"seed", RSeed<true>}, {"state", RState}, {"set_state", RSetState}, {"clone", RClone}, {nullptr, nullptr}};
		MakeClass(L, kRng, rm, nullptr, GcUdata<Pcg>);
		static const luaL_Reg top[] = {{"new", RNew}, {"hash", RHash}, {nullptr, nullptr}};
		luaL_newlib(L, top);
		// shared generator for the module level functions
		Pcg* g = NewUdata<Pcg>(L, kRng);
		std::random_device rd;
		g->Seed(((uint64_t)rd() << 32) | rd(), ((uint64_t)rd() << 32) | rd());
		lua_pushvalue(L, -1);
		lua_setfield(L, -3, "default");
		const std::pair<const char*, lua_CFunction> fns[] = {{"int", RInt<false>}, {"float", RFloat<false>}, {"range", RRange<false>}, {"normal", RNormal<false>},
			{"bool", RBool<false>}, {"pick", RPick<false>}, {"shuffle", RShuffle<false>}, {"weighted", RWeighted<false>}, {"sample", RSample<false>},
			{"unit2", RUnit2<false>}, {"unit3", RUnit3<false>}, {"in_circle", RInCircle<false>}, {"in_sphere", RInSphere<false>}, {"dice", RDice<false>},
			{"bytes", RBytes<false>}, {"seed", RSeed<false>}};
		for (const auto& f : fns)
		{
			lua_pushvalue(L, -1);
			lua_pushcclosure(L, f.second, 1);
			lua_setfield(L, -3, f.first);
		}
		lua_pop(L, 1);
		return 1;
	}

	// ================================================================== noise
	namespace
	{
		struct Perm
		{
			std::array<uint8_t, 512> p;
		};
		const Perm& PermFor(int seed)
		{
			static std::mutex mu;
			static std::map<int, Perm> cache;
			std::lock_guard<std::mutex> lock(mu);
			auto it = cache.find(seed);
			if (it != cache.end())
				return it->second;
			Perm pm;
			std::array<uint8_t, 256> base;
			for (int i = 0; i < 256; ++i) base[(size_t)i] = (uint8_t)i;
			Pcg g;
			g.Seed((uint64_t)(uint32_t)seed, 77);
			for (int i = 255; i > 0; --i)
				std::swap(base[(size_t)i], base[(size_t)g.Below((uint64_t)i + 1)]);
			for (int i = 0; i < 512; ++i) pm.p[(size_t)i] = base[(size_t)i & 255];
			if (cache.size() > 64) cache.clear();
			return cache.emplace(seed, pm).first->second;
		}

		double Fade(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }
		double LerpD(double t, double a, double b) { return a + t * (b - a); }
		double Grad3(int hash, double x, double y, double z)
		{
			const int h = hash & 15;
			const double u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
			return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
		}
		double Perlin3(const Perm& P, double x, double y, double z)
		{
			const int X = (int)std::floor(x) & 255, Y = (int)std::floor(y) & 255, Z = (int)std::floor(z) & 255;
			x -= std::floor(x); y -= std::floor(y); z -= std::floor(z);
			const double u = Fade(x), v = Fade(y), w = Fade(z);
			const auto& p = P.p;
			const int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z, B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
			return LerpD(w, LerpD(v, LerpD(u, Grad3(p[AA], x, y, z), Grad3(p[BA], x - 1, y, z)), LerpD(u, Grad3(p[AB], x, y - 1, z), Grad3(p[BB], x - 1, y - 1, z))),
				LerpD(v, LerpD(u, Grad3(p[AA + 1], x, y, z - 1), Grad3(p[BA + 1], x - 1, y, z - 1)), LerpD(u, Grad3(p[AB + 1], x, y - 1, z - 1), Grad3(p[BB + 1], x - 1, y - 1, z - 1))));
		}
		const int G3[12][3] = {{1, 1, 0}, {-1, 1, 0}, {1, -1, 0}, {-1, -1, 0}, {1, 0, 1}, {-1, 0, 1}, {1, 0, -1}, {-1, 0, -1}, {0, 1, 1}, {0, -1, 1}, {0, 1, -1}, {0, -1, -1}};
		double Simplex2(const Perm& P, double xin, double yin)
		{
			const double F2 = 0.5 * (std::sqrt(3.0) - 1.0), G2 = (3.0 - std::sqrt(3.0)) / 6.0;
			const double s = (xin + yin) * F2;
			const int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s);
			const double t = (i + j) * G2;
			const double x0 = xin - (i - t), y0 = yin - (j - t);
			const int i1 = x0 > y0 ? 1 : 0, j1 = x0 > y0 ? 0 : 1;
			const double x1 = x0 - i1 + G2, y1 = y0 - j1 + G2, x2 = x0 - 1 + 2 * G2, y2 = y0 - 1 + 2 * G2;
			const int ii = i & 255, jj = j & 255;
			const auto& p = P.p;
			auto contrib = [&](double x, double y, int gi) {
				double tt = 0.5 - x * x - y * y;
				if (tt < 0) return 0.0;
				tt *= tt;
				return tt * tt * (G3[gi][0] * x + G3[gi][1] * y);
			};
			return 70.0 * (contrib(x0, y0, p[ii + p[jj]] % 12) + contrib(x1, y1, p[ii + i1 + p[jj + j1]] % 12) + contrib(x2, y2, p[ii + 1 + p[jj + 1]] % 12));
		}
		double Simplex3(const Perm& P, double xin, double yin, double zin)
		{
			const double F3 = 1.0 / 3.0, G3c = 1.0 / 6.0;
			const double s = (xin + yin + zin) * F3;
			const int i = (int)std::floor(xin + s), j = (int)std::floor(yin + s), k = (int)std::floor(zin + s);
			const double t = (i + j + k) * G3c;
			const double x0 = xin - (i - t), y0 = yin - (j - t), z0 = zin - (k - t);
			int i1, j1, k1, i2, j2, k2;
			if (x0 >= y0)
			{
				if (y0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
				else if (x0 >= z0) { i1 = 1; j1 = 0; k1 = 0; i2 = 1; j2 = 0; k2 = 1; }
				else { i1 = 0; j1 = 0; k1 = 1; i2 = 1; j2 = 0; k2 = 1; }
			}
			else
			{
				if (y0 < z0) { i1 = 0; j1 = 0; k1 = 1; i2 = 0; j2 = 1; k2 = 1; }
				else if (x0 < z0) { i1 = 0; j1 = 1; k1 = 0; i2 = 0; j2 = 1; k2 = 1; }
				else { i1 = 0; j1 = 1; k1 = 0; i2 = 1; j2 = 1; k2 = 0; }
			}
			const double x1 = x0 - i1 + G3c, y1 = y0 - j1 + G3c, z1 = z0 - k1 + G3c;
			const double x2 = x0 - i2 + 2 * G3c, y2 = y0 - j2 + 2 * G3c, z2 = z0 - k2 + 2 * G3c;
			const double x3 = x0 - 1 + 3 * G3c, y3 = y0 - 1 + 3 * G3c, z3 = z0 - 1 + 3 * G3c;
			const int ii = i & 255, jj = j & 255, kk = k & 255;
			const auto& p = P.p;
			auto contrib = [&](double x, double y, double z, int gi) {
				double tt = 0.6 - x * x - y * y - z * z;
				if (tt < 0) return 0.0;
				tt *= tt;
				return tt * tt * (G3[gi][0] * x + G3[gi][1] * y + G3[gi][2] * z);
			};
			return 32.0 * (contrib(x0, y0, z0, p[ii + p[jj + p[kk]]] % 12) + contrib(x1, y1, z1, p[ii + i1 + p[jj + j1 + p[kk + k1]]] % 12) +
							  contrib(x2, y2, z2, p[ii + i2 + p[jj + j2 + p[kk + k2]]] % 12) + contrib(x3, y3, z3, p[ii + 1 + p[jj + 1 + p[kk + 1]]] % 12));
		}
		double Lat(const Perm& P, int x, int y, int z) { return (double)P.p[(P.p[(P.p[x & 255] + y) & 511] + z) & 511] / 255.0; }
		double Value3(const Perm& P, double x, double y, double z)
		{
			const int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
			const double fx = Fade(x - xi), fy = Fade(y - yi), fz = Fade(z - zi);
			auto l = [&](int dx, int dy, int dz) { return Lat(P, xi + dx, yi + dy, zi + dz); };
			const double r = LerpD(fz, LerpD(fy, LerpD(fx, l(0, 0, 0), l(1, 0, 0)), LerpD(fx, l(0, 1, 0), l(1, 1, 0))),
				LerpD(fy, LerpD(fx, l(0, 0, 1), l(1, 0, 1)), LerpD(fx, l(0, 1, 1), l(1, 1, 1))));
			return r * 2 - 1;
		}
		// Worley: returns F1, F2 distances, cell hash value
		void Worley3(const Perm& P, double x, double y, double z, double& f1, double& f2, double& cell)
		{
			const int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
			f1 = f2 = 1e9;
			cell = 0;
			for (int dz = -1; dz <= 1; ++dz)
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx)
					{
						const int cx = xi + dx, cy = yi + dy, cz = zi + dz;
						const uint32_t h = HashMix((uint32_t)cx * 73856093u ^ (uint32_t)cy * 19349663u ^ (uint32_t)cz * 83492791u ^ (uint32_t)P.p[0] * 2654435761u);
						const double px = cx + (double)(h & 1023) / 1023.0, py = cy + (double)((h >> 10) & 1023) / 1023.0, pz = cz + (double)((h >> 20) & 1023) / 1023.0;
						const double d = std::sqrt((px - x) * (px - x) + (py - y) * (py - y) + (pz - z) * (pz - z));
						if (d < f1) { f2 = f1; f1 = d; cell = (double)(h >> 8) / 16777216.0; }
						else if (d < f2) f2 = d;
					}
		}

		int Seed(lua_State* L, int i) { return (int)luaL_optinteger(L, i, 0); }
		int NPerlin2(lua_State* L) { lua_pushnumber(L, Perlin3(PermFor(Seed(L, 3)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), 0.5)); return 1; }
		int NPerlin3(lua_State* L) { lua_pushnumber(L, Perlin3(PermFor(Seed(L, 4)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3))); return 1; }
		int NSimplex2(lua_State* L) { lua_pushnumber(L, Simplex2(PermFor(Seed(L, 3)), luaL_checknumber(L, 1), luaL_checknumber(L, 2))); return 1; }
		int NSimplex3(lua_State* L) { lua_pushnumber(L, Simplex3(PermFor(Seed(L, 4)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3))); return 1; }
		int NValue2(lua_State* L) { lua_pushnumber(L, Value3(PermFor(Seed(L, 3)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), 0.0)); return 1; }
		int NValue3(lua_State* L) { lua_pushnumber(L, Value3(PermFor(Seed(L, 4)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3))); return 1; }
		int NWorley2(lua_State* L)
		{
			double f1, f2, c;
			Worley3(PermFor(Seed(L, 3)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), 0.5, f1, f2, c);
			lua_pushnumber(L, f1); lua_pushnumber(L, f2); lua_pushnumber(L, c);
			return 3;
		}
		int NWorley3(lua_State* L)
		{
			double f1, f2, c;
			Worley3(PermFor(Seed(L, 4)), luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), f1, f2, c);
			lua_pushnumber(L, f1); lua_pushnumber(L, f2); lua_pushnumber(L, c);
			return 3;
		}
		// fbm(kind, x, y, z, {octaves=4, lacunarity=2, gain=0.5, seed=0, mode="fbm|ridged|billow|turbulence"}) -> [-1,1] (turbulence/ridged/billow: [0,1])
		int NFbm(lua_State* L)
		{
			const std::string kind = Str(L, 1);
			const double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_optnumber(L, 4, 0.0);
			const int oct = (int)Clamp(FieldNum(L, 5, "octaves", 4), 1, 16);
			const double lac = FieldNum(L, 5, "lacunarity", 2), gain = FieldNum(L, 5, "gain", 0.5);
			const Perm& P = PermFor((int)FieldNum(L, 5, "seed", 0));
			const std::string mode = FieldStr(L, 5, "mode", "fbm");
			auto sample = [&](double a, double b, double c) {
				if (kind == "simplex") return Simplex3(P, a, b, c);
				if (kind == "value") return Value3(P, a, b, c);
				if (kind == "perlin") return Perlin3(P, a, b, c);
				return std::nan("");
			};
			if (std::isnan(sample(0, 0, 0)))
				return luaL_error(L, "unknown noise kind '%s' (perlin, simplex, value)", kind.c_str());
			double sum = 0, amp = 1, freq = 1, norm = 0, weight = 1;
			for (int i = 0; i < oct; ++i)
			{
				double n = sample(x * freq, y * freq, z * freq);
				if (mode == "ridged")
				{
					n = 1 - std::fabs(n);
					n *= n;
					n *= weight;
					weight = Clamp(n * 2, 0, 1);
				}
				else if (mode == "billow" || mode == "turbulence")
					n = std::fabs(n);
				sum += n * amp;
				norm += amp;
				amp *= gain;
				freq *= lac;
			}
			lua_pushnumber(L, sum / norm);
			return 1;
		}
		// curl2(x, y, {seed, eps}) -> vec2 divergence-free field from simplex noise
		int NCurl2(lua_State* L)
		{
			const double x = luaL_checknumber(L, 1), y = luaL_checknumber(L, 2), e = FieldNum(L, 3, "eps", 1e-3);
			const Perm& P = PermFor((int)FieldNum(L, 3, "seed", 0));
			const double dy = (Simplex2(P, x, y + e) - Simplex2(P, x, y - e)) / (2 * e), dx = (Simplex2(P, x + e, y) - Simplex2(P, x - e, y)) / (2 * e);
			PushVec2(L, dy, -dx);
			return 1;
		}
		// Fills a w*h array of perlin/simplex/value noise (row major, 1-based) - fast bulk path for terrain & textures
		int NGrid(lua_State* L)
		{
			const std::string kind = Str(L, 1);
			const lua_Integer w = luaL_checkinteger(L, 2), h = luaL_checkinteger(L, 3);
			luaL_argcheck(L, w > 0 && h > 0 && w * h <= 64 * 1024 * 1024, 2, "grid size out of range");
			const double scale = luaL_optnumber(L, 4, 0.05);
			const int oct = (int)Clamp(FieldNum(L, 5, "octaves", 4), 1, 16);
			const double lac = FieldNum(L, 5, "lacunarity", 2), gain = FieldNum(L, 5, "gain", 0.5);
			const double ox = FieldNum(L, 5, "x", 0), oy = FieldNum(L, 5, "y", 0), oz = FieldNum(L, 5, "z", 0);
			const Perm& P = PermFor((int)FieldNum(L, 5, "seed", 0));
			if (kind != "perlin" && kind != "simplex" && kind != "value")
				return luaL_error(L, "unknown noise kind '%s'", kind.c_str());
			lua_createtable(L, (int)(w * h), 0);
			lua_Integer idx = 1;
			for (lua_Integer j = 0; j < h; ++j)
				for (lua_Integer i = 0; i < w; ++i)
				{
					double sum = 0, amp = 1, freq = scale, norm = 0;
					for (int o = 0; o < oct; ++o)
					{
						const double a = (ox + (double)i) * freq, b = (oy + (double)j) * freq;
						sum += amp * (kind == "simplex" ? Simplex3(P, a, b, oz * freq) : kind == "value" ? Value3(P, a, b, oz * freq) : Perlin3(P, a, b, oz * freq + 0.5));
						norm += amp;
						amp *= gain;
						freq *= lac;
					}
					lua_pushnumber(L, sum / norm);
					lua_rawseti(L, -2, idx++);
				}
			return 1;
		}
	} // namespace

	int OpenNoise(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"perlin2", NPerlin2}, {"perlin3", NPerlin3}, {"simplex2", NSimplex2}, {"simplex3", NSimplex3}, {"value2", NValue2},
			{"value3", NValue3}, {"worley2", NWorley2}, {"worley3", NWorley3}, {"fbm", NFbm}, {"curl2", NCurl2}, {"grid", NGrid}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ================================================================== color
	namespace
	{
		struct Col { double r, g, b, a; };
		bool ReadCol(lua_State* L, int idx, Col& c)
		{
			idx = lua_absindex(L, idx);
			c = {0, 0, 0, 1};
			if (lua_type(L, idx) == LUA_TSTRING)
			{
				std::string s = Str(L, idx);
				if (!s.empty() && s[0] == '#') s.erase(0, 1);
				if (s.size() == 3 || s.size() == 4)
				{
					std::string e;
					for (char ch : s) { e += ch; e += ch; }
					s = e;
				}
				if (s.size() != 6 && s.size() != 8)
					return false;
				for (char ch : s)
					if (!isxdigit((unsigned char)ch))
						return false;
				auto h = [&](size_t i) { return (double)strtol(s.substr(i, 2).c_str(), nullptr, 16) / 255.0; };
				c = {h(0), h(2), h(4), s.size() == 8 ? h(6) : 1.0};
				return true;
			}
			double v[4];
			int n = 0;
			if (lua_istable(L, idx))
			{
				lua_getfield(L, idx, "r");
				const bool named = lua_isnumber(L, -1);
				lua_pop(L, 1);
				if (named)
				{
					c.r = FieldNum(L, idx, "r", 0); c.g = FieldNum(L, idx, "g", 0); c.b = FieldNum(L, idx, "b", 0); c.a = FieldNum(L, idx, "a", 1);
					return true;
				}
			}
			if (ToVec(L, idx, 3, v, &n))
			{
				c = {v[0], v[1], v[2], n >= 4 ? v[3] : 1.0};
				return true;
			}
			return false;
		}
		Col CheckCol(lua_State* L, int idx)
		{
			Col c;
			if (!ReadCol(L, idx, c))
				luaL_argerror(L, idx, "expected colour ({r,g,b,a} 0..1, \"#rrggbb[aa]\" or vec3/vec4)");
			return c;
		}
		void PushCol(lua_State* L, const Col& c)
		{
			lua_createtable(L, 0, 4);
			lua_pushnumber(L, c.r); lua_setfield(L, -2, "r");
			lua_pushnumber(L, c.g); lua_setfield(L, -2, "g");
			lua_pushnumber(L, c.b); lua_setfield(L, -2, "b");
			lua_pushnumber(L, c.a); lua_setfield(L, -2, "a");
		}
		double ToLin(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
		double ToSrgb(double c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }
		void RgbToHsv(const Col& c, double& h, double& s, double& v)
		{
			const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
			v = mx;
			s = mx <= 0 ? 0 : d / mx;
			if (d <= 0) h = 0;
			else if (mx == c.r) h = std::fmod((c.g - c.b) / d + 6, 6) / 6;
			else if (mx == c.g) h = ((c.b - c.r) / d + 2) / 6;
			else h = ((c.r - c.g) / d + 4) / 6;
		}
		Col HsvToRgb(double h, double s, double v, double a)
		{
			h = h - std::floor(h);
			const double i = std::floor(h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
			switch ((int)i % 6)
			{
			case 0: return {v, t, p, a};
			case 1: return {q, v, p, a};
			case 2: return {p, v, t, a};
			case 3: return {p, q, v, a};
			case 4: return {t, p, v, a};
			default: return {v, p, q, a};
			}
		}
		void RgbToHsl(const Col& c, double& h, double& s, double& l)
		{
			const double mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
			l = (mx + mn) / 2;
			s = d <= 0 ? 0 : d / (1 - std::fabs(2 * l - 1));
			double sv, vv;
			RgbToHsv(c, h, sv, vv);
		}
		Col HslToRgb(double h, double s, double l, double a)
		{
			const double c = (1 - std::fabs(2 * l - 1)) * s;
			h = h - std::floor(h);
			const double x = c * (1 - std::fabs(std::fmod(h * 6, 2) - 1)), m = l - c / 2;
			double r = 0, g = 0, b = 0;
			const int i = (int)(h * 6) % 6;
			switch (i)
			{
			case 0: r = c; g = x; break;
			case 1: r = x; g = c; break;
			case 2: g = c; b = x; break;
			case 3: g = x; b = c; break;
			case 4: r = x; b = c; break;
			default: r = c; b = x; break;
			}
			return {r + m, g + m, b + m, a};
		}
		struct Lab { double L, a, b; };
		Lab ToOklab(const Col& c)
		{
			const double r = ToLin(c.r), g = ToLin(c.g), b = ToLin(c.b);
			const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b), m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b),
						 s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
			return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s, 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s, 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
		}
		Col FromOklab(const Lab& o, double a)
		{
			const double l = std::pow(o.L + 0.3963377774 * o.a + 0.2158037573 * o.b, 3), m = std::pow(o.L - 0.1055613458 * o.a - 0.0638541728 * o.b, 3),
						 s = std::pow(o.L - 0.0894841775 * o.a - 1.2914855480 * o.b, 3);
			return {ToSrgb(Clamp(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, 0, 1)), ToSrgb(Clamp(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s, 0, 1)),
				ToSrgb(Clamp(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s, 0, 1)), a};
		}
		double Luminance(const Col& c) { return 0.2126 * ToLin(c.r) + 0.7152 * ToLin(c.g) + 0.0722 * ToLin(c.b); }

		int CFromHex(lua_State* L) { PushCol(L, CheckCol(L, 1)); return 1; }
		int CToHex(lua_State* L)
		{
			const Col c = CheckCol(L, 1);
			auto b = [](double v) { return (int)std::lround(Clamp(v, 0, 1) * 255); };
			if (OptBool(L, 2, false) || c.a < 1.0)
				Push(L, StrFormat("#%02x%02x%02x%02x", b(c.r), b(c.g), b(c.b), b(c.a)));
			else
				Push(L, StrFormat("#%02x%02x%02x", b(c.r), b(c.g), b(c.b)));
			return 1;
		}
		int CRgb(lua_State* L) // from 0-255 values
		{
			PushCol(L, {luaL_checknumber(L, 1) / 255.0, luaL_checknumber(L, 2) / 255.0, luaL_checknumber(L, 3) / 255.0, luaL_optnumber(L, 4, 255) / 255.0});
			return 1;
		}
		int CToHsv(lua_State* L) { double h, s, v; RgbToHsv(CheckCol(L, 1), h, s, v); lua_pushnumber(L, h); lua_pushnumber(L, s); lua_pushnumber(L, v); return 3; }
		int CFromHsv(lua_State* L) { PushCol(L, HsvToRgb(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 1))); return 1; }
		int CToHsl(lua_State* L) { double h, s, l; RgbToHsl(CheckCol(L, 1), h, s, l); lua_pushnumber(L, h); lua_pushnumber(L, s); lua_pushnumber(L, l); return 3; }
		int CFromHsl(lua_State* L) { PushCol(L, HslToRgb(luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3), luaL_optnumber(L, 4, 1))); return 1; }
		int CToLinear(lua_State* L) { Col c = CheckCol(L, 1); PushCol(L, {ToLin(c.r), ToLin(c.g), ToLin(c.b), c.a}); return 1; }
		int CToSrgb(lua_State* L) { Col c = CheckCol(L, 1); PushCol(L, {ToSrgb(c.r), ToSrgb(c.g), ToSrgb(c.b), c.a}); return 1; }
		// mix(a, b, t, space) space: "srgb" (default), "linear", "oklab"
		int CMix(lua_State* L)
		{
			const Col a = CheckCol(L, 1), b = CheckCol(L, 2);
			const double t = luaL_checknumber(L, 3);
			const std::string space = lua_isnoneornil(L, 4) ? "srgb" : Str(L, 4);
			const double al = a.a + (b.a - a.a) * t;
			if (space == "linear")
				PushCol(L, {ToSrgb(ToLin(a.r) + (ToLin(b.r) - ToLin(a.r)) * t), ToSrgb(ToLin(a.g) + (ToLin(b.g) - ToLin(a.g)) * t), ToSrgb(ToLin(a.b) + (ToLin(b.b) - ToLin(a.b)) * t), al});
			else if (space == "oklab")
			{
				const Lab x = ToOklab(a), y = ToOklab(b);
				PushCol(L, FromOklab({x.L + (y.L - x.L) * t, x.a + (y.a - x.a) * t, x.b + (y.b - x.b) * t}, al));
			}
			else
				PushCol(L, {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, al});
			return 1;
		}
		int CLighten(lua_State* L)
		{
			double h, s, l;
			const Col c = CheckCol(L, 1);
			RgbToHsl(c, h, s, l);
			PushCol(L, HslToRgb(h, s, Clamp(l + luaL_checknumber(L, 2), 0, 1), c.a));
			return 1;
		}
		int CSaturate(lua_State* L)
		{
			double h, s, l;
			const Col c = CheckCol(L, 1);
			RgbToHsl(c, h, s, l);
			PushCol(L, HslToRgb(h, Clamp(s + luaL_checknumber(L, 2), 0, 1), l, c.a));
			return 1;
		}
		int CRotateHue(lua_State* L)
		{
			double h, s, v;
			const Col c = CheckCol(L, 1);
			RgbToHsv(c, h, s, v);
			PushCol(L, HsvToRgb(h + luaL_checknumber(L, 2) / 360.0, s, v, c.a));
			return 1;
		}
		int CAlpha(lua_State* L) { Col c = CheckCol(L, 1); c.a = Clamp(luaL_checknumber(L, 2), 0, 1); PushCol(L, c); return 1; }
		int CLuminance(lua_State* L) { lua_pushnumber(L, Luminance(CheckCol(L, 1))); return 1; }
		int CContrast(lua_State* L)
		{
			const double a = Luminance(CheckCol(L, 1)), b = Luminance(CheckCol(L, 2));
			lua_pushnumber(L, (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05));
			return 1;
		}
		// readable text colour (black or white) for a background
		int CTextOn(lua_State* L)
		{
			PushCol(L, Luminance(CheckCol(L, 1)) > 0.179 ? Col{0, 0, 0, 1} : Col{1, 1, 1, 1});
			return 1;
		}
		int CToOklab(lua_State* L) { const Lab o = ToOklab(CheckCol(L, 1)); lua_pushnumber(L, o.L); lua_pushnumber(L, o.a); lua_pushnumber(L, o.b); return 3; }
		int CFromOklab(lua_State* L) { PushCol(L, FromOklab({luaL_checknumber(L, 1), luaL_checknumber(L, 2), luaL_checknumber(L, 3)}, luaL_optnumber(L, 4, 1))); return 1; }
		// palette(n, {saturation, value, hue}) evenly spread via the golden ratio
		int CPalette(lua_State* L)
		{
			const lua_Integer n = luaL_checkinteger(L, 1);
			luaL_argcheck(L, n >= 0 && n <= 4096, 1, "count out of range");
			const double s = FieldNum(L, 2, "saturation", 0.6), v = FieldNum(L, 2, "value", 0.95);
			double h = FieldNum(L, 2, "hue", 0.0);
			lua_createtable(L, (int)n, 0);
			for (lua_Integer i = 1; i <= n; ++i)
			{
				PushCol(L, HsvToRgb(h, s, v, 1));
				lua_rawseti(L, -2, i);
				h += 0.61803398875;
			}
			return 1;
		}
		// gradient(stops, t): stops = { {pos, colour}, ... } sorted by pos
		int CGradient(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			const double t = luaL_checknumber(L, 2);
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			if (n == 0) return luaL_error(L, "gradient has no stops");
			std::vector<std::pair<double, Col>> stops;
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				lua_rawgeti(L, -1, 1);
				const double p = luaL_checknumber(L, -1);
				lua_pop(L, 1);
				lua_rawgeti(L, -1, 2);
				Col c = CheckCol(L, -1);
				lua_pop(L, 2);
				stops.push_back({p, c});
			}
			std::sort(stops.begin(), stops.end(), [](auto& a, auto& b) { return a.first < b.first; });
			if (t <= stops.front().first) { PushCol(L, stops.front().second); return 1; }
			if (t >= stops.back().first) { PushCol(L, stops.back().second); return 1; }
			for (size_t i = 1; i < stops.size(); ++i)
				if (t <= stops[i].first)
				{
					const double span = stops[i].first - stops[i - 1].first, u = span <= 0 ? 0 : (t - stops[i - 1].first) / span;
					const Col &a = stops[i - 1].second, &b = stops[i].second;
					PushCol(L, {a.r + (b.r - a.r) * u, a.g + (b.g - a.g) * u, a.b + (b.b - a.b) * u, a.a + (b.a - a.a) * u});
					return 1;
				}
			PushCol(L, stops.back().second);
			return 1;
		}
		// kelvin(temperature) approximate blackbody colour (1000..40000K)
		int CKelvin(lua_State* L)
		{
			const double t = Clamp(luaL_checknumber(L, 1), 1000, 40000) / 100.0;
			double r, g, b;
			r = t <= 66 ? 255 : 329.698727446 * std::pow(t - 60, -0.1332047592);
			g = t <= 66 ? 99.4708025861 * std::log(t) - 161.1195681661 : 288.1221695283 * std::pow(t - 60, -0.0755148492);
			b = t >= 66 ? 255 : (t <= 19 ? 0 : 138.5177312231 * std::log(t - 10) - 305.0447927307);
			PushCol(L, {Clamp(r, 0, 255) / 255, Clamp(g, 0, 255) / 255, Clamp(b, 0, 255) / 255, 1});
			return 1;
		}
	} // namespace

	int OpenColor(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"parse", CFromHex}, {"to_hex", CToHex}, {"rgb", CRgb}, {"to_hsv", CToHsv}, {"from_hsv", CFromHsv}, {"to_hsl", CToHsl},
			{"from_hsl", CFromHsl}, {"to_linear", CToLinear}, {"to_srgb", CToSrgb}, {"mix", CMix}, {"lighten", CLighten}, {"saturate", CSaturate},
			{"rotate_hue", CRotateHue}, {"with_alpha", CAlpha}, {"luminance", CLuminance}, {"contrast", CContrast}, {"text_on", CTextOn},
			{"to_oklab", CToOklab}, {"from_oklab", CFromOklab}, {"palette", CPalette}, {"gradient", CGradient}, {"kelvin", CKelvin}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}

	// ================================================================== utf8x
	namespace
	{
		uint32_t DecodeAt(std::string_view s, size_t& i)
		{
			const unsigned char c = (unsigned char)s[i];
			uint32_t cp;
			int len;
			if (c < 0x80) { cp = c; len = 1; }
			else if ((c >> 5) == 6) { cp = c & 0x1f; len = 2; }
			else if ((c >> 4) == 14) { cp = c & 0x0f; len = 3; }
			else if ((c >> 3) == 30) { cp = c & 0x07; len = 4; }
			else { ++i; return 0xFFFD; }
			if (i + (size_t)len > s.size()) { i = s.size(); return 0xFFFD; }
			for (int k = 1; k < len; ++k)
			{
				const unsigned char cc = (unsigned char)s[i + (size_t)k];
				if ((cc & 0xC0) != 0x80) { ++i; return 0xFFFD; }
				cp = (cp << 6) | (cc & 0x3f);
			}
			i += (size_t)len;
			return cp;
		}
		void Encode(std::string& out, uint32_t cp)
		{
			if (cp < 0x80) out += (char)cp;
			else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3f)); }
			else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3f)); out += (char)(0x80 | (cp & 0x3f)); }
			else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3f)); out += (char)(0x80 | ((cp >> 6) & 0x3f)); out += (char)(0x80 | (cp & 0x3f)); }
		}
		uint32_t Lower(uint32_t c)
		{
			if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
			if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 32;
			if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178 && c != 0x17F)
			{
				if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
				return (c & 1) ? c : c + 1;
			}
			if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
			if (c >= 0x410 && c <= 0x42F) return c + 32;
			if (c >= 0x400 && c <= 0x40F) return c + 80;
			return c;
		}
		uint32_t Upper(uint32_t c)
		{
			if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
			if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
			if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x178 && c != 0x17F)
			{
				if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
				return (c & 1) ? c - 1 : c;
			}
			if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
			if (c >= 0x430 && c <= 0x44F) return c - 32;
			if (c >= 0x450 && c <= 0x45F) return c - 80;
			return c;
		}
		int Width(uint32_t c)
		{
			if (c == 0) return 0;
			if (c < 32 || (c >= 0x7f && c < 0xa0)) return 0;
			if ((c >= 0x300 && c <= 0x36F) || (c >= 0x200B && c <= 0x200F) || (c >= 0xFE00 && c <= 0xFE0F)) return 0;
			if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3) || (c >= 0xF900 && c <= 0xFAFF) ||
				(c >= 0xFE30 && c <= 0xFE6F) || (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6) || (c >= 0x1F300 && c <= 0x1F64F) ||
				(c >= 0x1F900 && c <= 0x1F9FF) || (c >= 0x20000 && c <= 0x3FFFD))
				return 2;
			return 1;
		}
		std::vector<uint32_t> Decode(std::string_view s)
		{
			std::vector<uint32_t> v;
			size_t i = 0;
			while (i < s.size()) v.push_back(DecodeAt(s, i));
			return v;
		}
		std::string EncodeAll(const std::vector<uint32_t>& v, size_t from, size_t to)
		{
			std::string out;
			for (size_t i = from; i < to && i < v.size(); ++i) Encode(out, v[i]);
			return out;
		}
		int U8Upper(lua_State* L) { std::string o; for (uint32_t c : Decode(View(L, 1))) Encode(o, Upper(c)); Push(L, o); return 1; }
		int U8Lower(lua_State* L) { std::string o; for (uint32_t c : Decode(View(L, 1))) Encode(o, Lower(c)); Push(L, o); return 1; }
		int U8Len(lua_State* L) { lua_pushinteger(L, (lua_Integer)Decode(View(L, 1)).size()); return 1; }
		int U8Sub(lua_State* L)
		{
			const std::vector<uint32_t> v = Decode(View(L, 1));
			lua_Integer i = luaL_optinteger(L, 2, 1), j = luaL_optinteger(L, 3, -1);
			const lua_Integer n = (lua_Integer)v.size();
			if (i < 0) i = std::max<lua_Integer>(n + i + 1, 1);
			if (j < 0) j = n + j + 1;
			if (i < 1) i = 1;
			if (j > n) j = n;
			Push(L, i > j ? std::string() : EncodeAll(v, (size_t)i - 1, (size_t)j));
			return 1;
		}
		int U8Reverse(lua_State* L)
		{
			std::vector<uint32_t> v = Decode(View(L, 1));
			std::reverse(v.begin(), v.end());
			Push(L, EncodeAll(v, 0, v.size()));
			return 1;
		}
		int U8Valid(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			size_t i = 0;
			while (i < s.size())
			{
				const size_t start = i;
				const uint32_t c = DecodeAt(s, i);
				if (c == 0xFFFD && !(i - start == 3 && (unsigned char)s[start] == 0xEF))
				{
					lua_pushboolean(L, 0);
					lua_pushinteger(L, (lua_Integer)start + 1);
					return 2;
				}
			}
			lua_pushboolean(L, 1);
			return 1;
		}
		int U8Width(lua_State* L)
		{
			lua_Integer w = 0;
			for (uint32_t c : Decode(View(L, 1))) w += Width(c);
			lua_pushinteger(L, w);
			return 1;
		}
		// truncate(s, maxWidth, ellipsis="…") by display width
		int U8Truncate(lua_State* L)
		{
			const std::vector<uint32_t> v = Decode(View(L, 1));
			const lua_Integer maxw = luaL_checkinteger(L, 2);
			const std::string ell = lua_isnoneornil(L, 3) ? "\xE2\x80\xA6" : Str(L, 3);
			lua_Integer total = 0;
			for (uint32_t c : v) total += Width(c);
			if (total <= maxw) { lua_settop(L, 1); return 1; }
			lua_Integer ew = 0;
			for (uint32_t c : Decode(ell)) ew += Width(c);
			lua_Integer w = 0;
			size_t k = 0;
			while (k < v.size() && w + Width(v[k]) + ew <= maxw) w += Width(v[k++]);
			Push(L, EncodeAll(v, 0, k) + ell);
			return 1;
		}
		// codepoints(s) -> array ; from_codepoints(array) -> string
		int U8Codepoints(lua_State* L)
		{
			const std::vector<uint32_t> v = Decode(View(L, 1));
			lua_createtable(L, (int)v.size(), 0);
			for (size_t i = 0; i < v.size(); ++i) { lua_pushinteger(L, v[i]); lua_rawseti(L, -2, (lua_Integer)i + 1); }
			return 1;
		}
		int U8FromCodepoints(lua_State* L)
		{
			luaL_checktype(L, 1, LUA_TTABLE);
			std::string o;
			const lua_Integer n = (lua_Integer)luaL_len(L, 1);
			for (lua_Integer i = 1; i <= n; ++i)
			{
				lua_rawgeti(L, 1, i);
				const lua_Integer c = luaL_checkinteger(L, -1);
				luaL_argcheck(L, c >= 0 && c <= 0x10FFFF, 1, "invalid code point");
				Encode(o, (uint32_t)c);
				lua_pop(L, 1);
			}
			Push(L, o);
			return 1;
		}
		// chars(s) -> iterator over characters
		int U8CharsIter(lua_State* L)
		{
			const std::string_view s = View(L, 1);
			size_t i = (size_t)lua_tointeger(L, 2);
			if (i >= s.size()) return 0;
			size_t j = i;
			DecodeAt(s, j);
			lua_pushinteger(L, (lua_Integer)j);
			Push(L, s.substr(i, j - i));
			return 2;
		}
		int U8Chars(lua_State* L)
		{
			luaL_checkstring(L, 1);
			lua_pushcfunction(L, U8CharsIter);
			lua_pushvalue(L, 1);
			lua_pushinteger(L, 0);
			return 3;
		}
		int U8Capitalize(lua_State* L)
		{
			std::vector<uint32_t> v = Decode(View(L, 1));
			bool start = true;
			for (uint32_t& c : v)
			{
				if (start) c = Upper(c); else c = Lower(c);
				start = c == ' ' || c == '\t' || c == '\n' || c == '-';
			}
			Push(L, EncodeAll(v, 0, v.size()));
			return 1;
		}
		int U8Pad(lua_State* L)
		{
			const std::string s = Str(L, 1);
			const lua_Integer w = luaL_checkinteger(L, 2);
			const std::string mode = lua_isnoneornil(L, 3) ? "left" : Str(L, 3);
			const std::string fill = lua_isnoneornil(L, 4) ? " " : Str(L, 4);
			lua_Integer cur = 0;
			for (uint32_t c : Decode(s)) cur += Width(c);
			const lua_Integer pad = std::max<lua_Integer>(0, w - cur);
			auto rep = [&](lua_Integer n) { std::string r; for (lua_Integer i = 0; i < n; ++i) r += fill; return r; };
			if (mode == "right") Push(L, rep(pad) + s);
			else if (mode == "center") Push(L, rep(pad / 2) + s + rep(pad - pad / 2));
			else Push(L, s + rep(pad));
			return 1;
		}
	} // namespace

	int OpenUtf8x(lua_State* L)
	{
		static const luaL_Reg fns[] = {{"upper", U8Upper}, {"lower", U8Lower}, {"len", U8Len}, {"sub", U8Sub}, {"reverse", U8Reverse}, {"is_valid", U8Valid},
			{"width", U8Width}, {"truncate", U8Truncate}, {"codepoints", U8Codepoints}, {"from_codepoints", U8FromCodepoints}, {"chars", U8Chars},
			{"capitalize", U8Capitalize}, {"pad", U8Pad}, {nullptr, nullptr}};
		luaL_newlib(L, fns);
		return 1;
	}
} // namespace lumen::lua::detail
