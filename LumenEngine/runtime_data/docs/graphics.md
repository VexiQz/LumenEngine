# Graphics

Rendering uses Wicked Engine (Vulkan / DX12). Project settings > Graphics controls API, VSync, culling, shadows, reflections,
anti-aliasing, tonemap (ACES, Uchimura, Reinhard), exposure, saturation, contrast, bloom, dithering and eye adaptation.

**Sky** (Environment panel): *Softness* widens the sun glow and desaturates the Rayleigh blue, *Haze* scales aerial perspective,
clouds use gentler scattering at high softness. Dithering removes banding in smooth gradients. If the sky still looks harsh lower
the sun intensity or exposure, raise Softness / Haze, or try the Uchimura tonemap.

**Shaders**: the library in `shaders/lumen` (PBR, toon, water, glass, ice, foliage, terrain, dissolve, force field, unlit, post grade)
ships as HLSL. The built in light shaders can be switched off in the Shaders panel; write your own in the project's `shaders/` folder.
