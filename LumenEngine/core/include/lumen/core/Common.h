// Lumen Engine — common definitions shared by every module.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define LUMEN_VERSION_MAJOR 1
#define LUMEN_VERSION_MINOR 9
#define LUMEN_VERSION_PATCH 2
#define LUMEN_VERSION_STRING "1.9.2"

// Version of the .lum project format written by this build.
#define LUMEN_PROJECT_FORMAT_VERSION 1
// Version of the native module C ABI (see runtime_data/include/lumen_c.h).
#define LUMEN_NATIVE_API_VERSION 1

#if defined(__GNUC__) || defined(__clang__)
#define LUMEN_PRINTF_LIKE(fmtIndex, argIndex) __attribute__((format(printf, fmtIndex, argIndex)))
#define LUMEN_LIKELY(x) __builtin_expect(!!(x), 1)
#define LUMEN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LUMEN_PRINTF_LIKE(fmtIndex, argIndex)
#define LUMEN_LIKELY(x) (x)
#define LUMEN_UNLIKELY(x) (x)
#endif

#if defined(_WIN32)
#define LUMEN_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#define LUMEN_PLATFORM_MACOS 1
#else
#define LUMEN_PLATFORM_LINUX 1
#endif

namespace lumen
{

	// Result of an operation that can fail with a human readable reason.
	struct Status
	{
		bool ok = true;
		std::string message;

		static Status Ok() { return Status{}; }
		static Status Fail(std::string msg)
		{
			Status s;
			s.ok = false;
			s.message = std::move(msg);
			return s;
		}
		explicit operator bool() const { return ok; }
	};

	// Plain vector types used by the data model (scene documents, animation clips, settings).
	// The engine converts them to DirectXMath types at the Wicked boundary.
	struct Float2
	{
		float x = 0, y = 0;
	};
	struct Float3
	{
		float x = 0, y = 0, z = 0;
		bool operator==(const Float3& o) const { return x == o.x && y == o.y && z == o.z; }
		bool operator!=(const Float3& o) const { return !(*this == o); }
	};
	struct Float4
	{
		float x = 0, y = 0, z = 0, w = 0;
		bool operator==(const Float4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
		bool operator!=(const Float4& o) const { return !(*this == o); }
	};

	// printf style formatting into a std::string.
	std::string StrFormat(const char* fmt, ...) LUMEN_PRINTF_LIKE(1, 2);

} // namespace lumen
