// Minimal self-registering test framework (no exceptions; failures are recorded and the test continues).
#pragma once
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace lumentest
{
	struct Case { const char* suite; const char* name; void (*fn)(); };
	inline std::vector<Case>& Registry() { static std::vector<Case> r; return r; }
	inline int& Failures() { static int f = 0; return f; }
	inline int& Checks() { static int c = 0; return c; }
	struct Registrar { Registrar(const char* s, const char* n, void (*f)()) { Registry().push_back({s, n, f}); } };

	inline int RunAll(const char* filter)
	{
		int failedCases = 0, ran = 0;
		for (const Case& c : Registry())
		{
			const std::string full = std::string(c.suite) + "." + c.name;
			if (filter && *filter && full.find(filter) == std::string::npos)
				continue;
			const int before = Failures();
			c.fn();
			++ran;
			if (Failures() != before) { ++failedCases; std::printf("[FAIL] %s\n", full.c_str()); }
			else std::printf("[ ok ] %s\n", full.c_str());
		}
		std::printf("\n%d tests, %d failed, %d checks\n", ran, failedCases, Checks());
		return failedCases ? 1 : 0;
	}
}

#define LT_CONCAT2(a, b) a##b
#define LT_CONCAT(a, b) LT_CONCAT2(a, b)
#define TEST(suite, name) \
	static void LT_CONCAT(test_##suite##_, name)(); \
	static lumentest::Registrar LT_CONCAT(reg_##suite##_, name)(#suite, #name, &LT_CONCAT(test_##suite##_, name)); \
	static void LT_CONCAT(test_##suite##_, name)()
#define CHECK(cond) do { ++lumentest::Checks(); if (!(cond)) { ++lumentest::Failures(); std::printf("  %s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { ++lumentest::Checks(); if (!((a) == (b))) { ++lumentest::Failures(); std::printf("  %s:%d: CHECK_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b); } } while (0)
