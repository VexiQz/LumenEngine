// Lumen Engine — SHA-256 (FIPS 180-4), used to verify downloaded packages and to key caches.
#pragma once

#include "lumen/core/Common.h"

namespace lumen
{
	class Sha256
	{
	public:
		Sha256() { Reset(); }
		void Reset();
		void Update(const void* data, size_t size);
		void Update(std::string_view s) { Update(s.data(), s.size()); }
		// Finalizes and returns the 32 byte digest. The object must be Reset() before reuse.
		void Final(uint8_t out[32]);
		std::string FinalHex();

		static std::string HashHex(const void* data, size_t size);
		static std::string HashHex(std::string_view s) { return HashHex(s.data(), s.size()); }
		// Hex digest of a file's content; empty string when the file cannot be read.
		static std::string HashFileHex(const std::string& path);

	private:
		void Transform(const uint8_t block[64]);
		uint32_t state_[8];
		uint64_t bitCount_;
		uint8_t buffer_[64];
		size_t bufferLen_;
	};

} // namespace lumen
