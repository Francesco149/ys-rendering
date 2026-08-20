// dds_loader.h — High-performance DirectDraw Surface (DDS) parser and decompressor
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <raylib.h>

struct lua_State;

namespace falcom {

class DdsLoader {
public:
    // Decodes DDS memory buffer to a Raylib Image (RGBA8888).
    // auto_lum_alpha: if true, calculates alpha from luminance for additive blending (e.g. Z_ light shafts)
    static Image load_image_from_memory(const uint8_t* data, size_t size, bool auto_lum_alpha = false);

    // Decodes DDS memory buffer directly to a GPU Texture2D
    static Texture2D load_texture_from_memory(const uint8_t* data, size_t size, bool auto_lum_alpha = false);

    // Helper to decompress DXT1 blocks to RGBA
    static void decompress_dxt1(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst);

    // Helper to decompress DXT3 blocks to RGBA
    static void decompress_dxt3(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst);

    // Helper to decompress DXT5 blocks to RGBA
    static void decompress_dxt5(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst);
};

void register_dds_lua(lua_State* L);

} // namespace falcom
