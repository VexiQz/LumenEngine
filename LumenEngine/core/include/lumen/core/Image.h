// Lumen Engine — tiny RGBA8 image helper: PNG encoder and procedural texture generators.
//
// Project templates use these to ship real starter textures (grid, checker, flat normal, tileable water
// normal map, tileable noise) without binary blobs in the repository.
#pragma once

#include "lumen/core/Common.h"

namespace lumen::image
{
	struct Image
	{
		int width = 0;
		int height = 0;
		std::vector<uint8_t> rgba; // width * height * 4

		bool Valid() const { return width > 0 && height > 0 && rgba.size() == (size_t)width * (size_t)height * 4; }
		uint8_t* Pixel(int x, int y) { return &rgba[((size_t)y * (size_t)width + (size_t)x) * 4]; }
		const uint8_t* Pixel(int x, int y) const { return &rgba[((size_t)y * (size_t)width + (size_t)x) * 4]; }
	};

	Image MakeSolid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
	// Neutral grey texture with darker grid lines every size/cells pixels.
	Image MakeGrid(int size, int cells);
	Image MakeChecker(int size, int cells);
	// Tileable tangent space normal map made of several sine wave trains (OpenGL style, +Y up).
	Image MakeWaterNormalMap(int size, uint32_t seed, float strength);
	// Tileable fractal value noise stored in R=G=B.
	Image MakeValueNoise(int size, uint32_t seed, int octaves);

	// CRC-32 (PNG / zlib polynomial) and Adler-32.
	uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0);
	uint32_t Adler32(const uint8_t* data, size_t size, uint32_t adler = 1);

	// Lossless PNG with stored (uncompressed) deflate blocks: simple, fast and always valid.
	std::vector<uint8_t> EncodePng(const Image& image);
	bool WritePng(const std::string& path, const Image& image);

} // namespace lumen::image
