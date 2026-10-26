#include "lumen/core/Image.h"

#include "lumen/core/Fs.h"

#include <algorithm>
#include <cmath>

namespace lumen::image
{
	namespace
	{
		constexpr double kPi = 3.14159265358979323846;

		const uint32_t* CrcTable()
		{
			static uint32_t table[256];
			static bool init = false;
			if (!init)
			{
				for (uint32_t n = 0; n < 256; ++n)
				{
					uint32_t c = n;
					for (int k = 0; k < 8; ++k)
						c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
					table[n] = c;
				}
				init = true;
			}
			return table;
		}

		// xorshift based deterministic generator so that generated textures are reproducible.
		struct Rng
		{
			uint32_t s;
			explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
			uint32_t Next()
			{
				s ^= s << 13;
				s ^= s >> 17;
				s ^= s << 5;
				return s;
			}
			float Float() { return (float)(Next() & 0xFFFFFF) / (float)0x1000000; }
		};

		void PutU32BE(std::vector<uint8_t>& v, uint32_t x)
		{
			v.push_back((uint8_t)(x >> 24));
			v.push_back((uint8_t)(x >> 16));
			v.push_back((uint8_t)(x >> 8));
			v.push_back((uint8_t)x);
		}

		void AppendChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& data)
		{
			PutU32BE(out, (uint32_t)data.size());
			const size_t typeStart = out.size();
			out.insert(out.end(), type, type + 4);
			out.insert(out.end(), data.begin(), data.end());
			PutU32BE(out, Crc32(out.data() + typeStart, 4 + data.size()));
		}
	} // namespace

	uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc)
	{
		const uint32_t* table = CrcTable();
		uint32_t c = crc ^ 0xFFFFFFFFu;
		for (size_t i = 0; i < size; ++i)
			c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
		return c ^ 0xFFFFFFFFu;
	}

	uint32_t Adler32(const uint8_t* data, size_t size, uint32_t adler)
	{
		uint32_t a = adler & 0xFFFF;
		uint32_t b = (adler >> 16) & 0xFFFF;
		for (size_t i = 0; i < size; ++i)
		{
			a = (a + data[i]) % 65521;
			b = (b + a) % 65521;
		}
		return (b << 16) | a;
	}

	std::vector<uint8_t> EncodePng(const Image& image)
	{
		std::vector<uint8_t> out;
		if (!image.Valid())
			return out;
		static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
		out.insert(out.end(), signature, signature + 8);

		std::vector<uint8_t> ihdr;
		PutU32BE(ihdr, (uint32_t)image.width);
		PutU32BE(ihdr, (uint32_t)image.height);
		ihdr.push_back(8); // bit depth
		ihdr.push_back(6); // RGBA
		ihdr.push_back(0); // deflate
		ihdr.push_back(0); // filter method
		ihdr.push_back(0); // no interlace
		AppendChunk(out, "IHDR", ihdr);

		// Raw scanlines with filter type 0.
		const size_t rowBytes = (size_t)image.width * 4;
		std::vector<uint8_t> raw;
		raw.reserve((rowBytes + 1) * (size_t)image.height);
		for (int y = 0; y < image.height; ++y)
		{
			raw.push_back(0);
			raw.insert(raw.end(), image.rgba.begin() + (ptrdiff_t)((size_t)y * rowBytes), image.rgba.begin() + (ptrdiff_t)((size_t)(y + 1) * rowBytes));
		}

		// zlib stream made of stored blocks.
		std::vector<uint8_t> z;
		z.push_back(0x78);
		z.push_back(0x01);
		size_t pos = 0;
		do
		{
			const size_t n = std::min<size_t>(65535, raw.size() - pos);
			const bool last = pos + n >= raw.size();
			z.push_back(last ? 1 : 0);
			z.push_back((uint8_t)(n & 0xFF));
			z.push_back((uint8_t)(n >> 8));
			z.push_back((uint8_t)(~n & 0xFF));
			z.push_back((uint8_t)((~n >> 8) & 0xFF));
			z.insert(z.end(), raw.begin() + (ptrdiff_t)pos, raw.begin() + (ptrdiff_t)(pos + n));
			pos += n;
		} while (pos < raw.size());
		PutU32BE(z, Adler32(raw.data(), raw.size()));
		AppendChunk(out, "IDAT", z);
		AppendChunk(out, "IEND", {});
		return out;
	}

	bool WritePng(const std::string& path, const Image& image)
	{
		const std::vector<uint8_t> bytes = EncodePng(image);
		return !bytes.empty() && fs::WriteBinary(path, bytes.data(), bytes.size());
	}

	Image MakeSolid(int width, int height, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		Image img;
		img.width = std::max(1, width);
		img.height = std::max(1, height);
		img.rgba.resize((size_t)img.width * (size_t)img.height * 4);
		for (size_t i = 0; i < img.rgba.size(); i += 4)
		{
			img.rgba[i] = r;
			img.rgba[i + 1] = g;
			img.rgba[i + 2] = b;
			img.rgba[i + 3] = a;
		}
		return img;
	}

	Image MakeGrid(int size, int cells)
	{
		Image img = MakeSolid(size, size, 188, 188, 192);
		cells = std::max(1, cells);
		const int cell = std::max(1, size / cells);
		const int minorStep = std::max(1, cell / 4);
		const int thickness = std::max(1, size / 256);
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const bool major = (x % cell) < thickness || (y % cell) < thickness;
				const bool minor = !major && (((x % minorStep) == 0) || ((y % minorStep) == 0));
				uint8_t* p = img.Pixel(x, y);
				if (major)
				{
					p[0] = p[1] = p[2] = 70;
				}
				else if (minor)
				{
					p[0] = p[1] = p[2] = 160;
				}
			}
		}
		return img;
	}

	Image MakeChecker(int size, int cells)
	{
		Image img = MakeSolid(size, size, 255, 255, 255);
		cells = std::max(1, cells);
		const int cell = std::max(1, size / cells);
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const bool dark = (((x / cell) + (y / cell)) & 1) != 0;
				uint8_t* p = img.Pixel(x, y);
				p[0] = p[1] = p[2] = dark ? 90 : 215;
			}
		}
		return img;
	}

	Image MakeWaterNormalMap(int size, uint32_t seed, float strength)
	{
		Image img = MakeSolid(size, size, 128, 128, 255);
		struct Wave
		{
			double fx, fy, amp, phase;
		};
		Rng rng(seed);
		std::vector<Wave> waves;
		// Integer frequencies make the height field periodic over the texture, so it tiles seamlessly.
		for (int i = 0; i < 14; ++i)
		{
			const double octave = 1.0 + (double)(i / 3);
			const double fx = std::round((rng.Float() * 2.0 - 1.0) * 5.0 * octave);
			const double fy = std::round((rng.Float() * 2.0 - 1.0) * 5.0 * octave);
			if (fx == 0 && fy == 0)
				continue;
			const double amp = 1.0 / (octave * octave * 1.8);
			waves.push_back({fx, fy, amp, rng.Float() * 2.0 * kPi});
		}
		const double inv = 2.0 * kPi / (double)size;
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				double dhdx = 0, dhdy = 0;
				for (const Wave& w : waves)
				{
					const double arg = (w.fx * x + w.fy * y) * inv + w.phase;
					const double c = std::cos(arg) * w.amp;
					dhdx += c * w.fx * inv;
					dhdy += c * w.fy * inv;
				}
				// Scale so that the strongest slopes are around 45 degrees when strength == 1.
				const double s = (double)strength * (double)size * 0.012;
				double nx = -dhdx * s;
				double ny = -dhdy * s;
				double nz = 1.0;
				const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
				nx /= len;
				ny /= len;
				nz /= len;
				uint8_t* p = img.Pixel(x, y);
				p[0] = (uint8_t)std::lround((nx * 0.5 + 0.5) * 255.0);
				p[1] = (uint8_t)std::lround((ny * 0.5 + 0.5) * 255.0);
				p[2] = (uint8_t)std::lround((nz * 0.5 + 0.5) * 255.0);
				p[3] = 255;
			}
		}
		return img;
	}

	Image MakeValueNoise(int size, uint32_t seed, int octaves)
	{
		Image img = MakeSolid(size, size, 0, 0, 0);
		octaves = std::max(1, std::min(octaves, 8));
		std::vector<float> field((size_t)size * (size_t)size, 0.0f);
		float amplitude = 1.0f;
		float total = 0.0f;
		for (int o = 0; o < octaves; ++o)
		{
			const int cells = 4 << o; // lattice resolution of this octave
			Rng rng(seed + (uint32_t)o * 7919u);
			std::vector<float> lattice((size_t)cells * (size_t)cells);
			for (float& v : lattice)
				v = rng.Float();
			for (int y = 0; y < size; ++y)
			{
				const float fy = (float)y / (float)size * (float)cells;
				const int y0 = (int)fy % cells;
				const int y1 = (y0 + 1) % cells;
				float ty = fy - std::floor(fy);
				ty = ty * ty * (3.0f - 2.0f * ty);
				for (int x = 0; x < size; ++x)
				{
					const float fx = (float)x / (float)size * (float)cells;
					const int x0 = (int)fx % cells;
					const int x1 = (x0 + 1) % cells;
					float tx = fx - std::floor(fx);
					tx = tx * tx * (3.0f - 2.0f * tx);
					const float a = lattice[(size_t)y0 * cells + x0];
					const float b = lattice[(size_t)y0 * cells + x1];
					const float c = lattice[(size_t)y1 * cells + x0];
					const float d = lattice[(size_t)y1 * cells + x1];
					const float v = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
					field[(size_t)y * size + x] += v * amplitude;
				}
			}
			total += amplitude;
			amplitude *= 0.5f;
		}
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float v = std::min(1.0f, std::max(0.0f, field[(size_t)y * size + x] / total));
				uint8_t* p = img.Pixel(x, y);
				p[0] = p[1] = p[2] = (uint8_t)std::lround(v * 255.0f);
			}
		}
		return img;
	}

} // namespace lumen::image
