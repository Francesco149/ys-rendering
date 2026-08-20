// dds_loader.cpp — DirectDraw Surface (DDS) parser and decompressor implementation
#include "dds_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace falcom {

#pragma pack(push, 1)
struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t dwMagic; // "DDS " (0x20534444)
    uint32_t dwSize;  // 124
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
    uint32_t dwReserved2;
};
#pragma pack(pop)

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static inline uint8_t clamp_u8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static inline void decode_rgb565(uint16_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
    b = (uint8_t)((c & 0x1F) * 255 / 31);
}

void DdsLoader::decompress_dxt1(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst) {
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    const uint8_t* block_ptr = src;

    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint16_t c0 = *(const uint16_t*)(block_ptr + 0);
            uint16_t c1 = *(const uint16_t*)(block_ptr + 2);
            uint32_t bits = *(const uint32_t*)(block_ptr + 4);
            block_ptr += 8;

            uint8_t r[4], g[4], b[4], a[4];
            decode_rgb565(c0, r[0], g[0], b[0]); a[0] = 255;
            decode_rgb565(c1, r[1], g[1], b[1]); a[1] = 255;

            if (c0 > c1) {
                r[2] = (uint8_t)((2 * r[0] + r[1]) / 3);
                g[2] = (uint8_t)((2 * g[0] + g[1]) / 3);
                b[2] = (uint8_t)((2 * b[0] + b[1]) / 3);
                a[2] = 255;

                r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3);
                g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3);
                b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3);
                a[3] = 255;
            } else {
                r[2] = (uint8_t)((r[0] + r[1]) / 2);
                g[2] = (uint8_t)((g[0] + g[1]) / 2);
                b[2] = (uint8_t)((b[0] + b[1]) / 2);
                a[2] = 255;

                r[3] = 0; g[3] = 0; b[3] = 0; a[3] = 0;
            }

            for (int y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                for (int x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    int shift = (y * 4 + x) * 2;
                    uint8_t idx = (bits >> shift) & 3;

                    uint32_t dst_idx = (py * width + px) * 4;
                    dst[dst_idx + 0] = r[idx];
                    dst[dst_idx + 1] = g[idx];
                    dst[dst_idx + 2] = b[idx];
                    dst[dst_idx + 3] = a[idx];
                }
            }
        }
    }
}

void DdsLoader::decompress_dxt3(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst) {
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    const uint8_t* block_ptr = src;

    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            const uint16_t* alpha_rows = (const uint16_t*)block_ptr;
            block_ptr += 8;

            uint16_t c0 = *(const uint16_t*)(block_ptr + 0);
            uint16_t c1 = *(const uint16_t*)(block_ptr + 2);
            uint32_t bits = *(const uint32_t*)(block_ptr + 4);
            block_ptr += 8;

            uint8_t r[4], g[4], b[4];
            decode_rgb565(c0, r[0], g[0], b[0]);
            decode_rgb565(c1, r[1], g[1], b[1]);

            r[2] = (uint8_t)((2 * r[0] + r[1]) / 3);
            g[2] = (uint8_t)((2 * g[0] + g[1]) / 3);
            b[2] = (uint8_t)((2 * b[0] + b[1]) / 3);

            r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3);
            g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3);
            b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3);

            for (int y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;
                uint16_t alpha_row = alpha_rows[y];

                for (int x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    uint8_t a4 = (alpha_row >> (x * 4)) & 0xF;
                    uint8_t a = (uint8_t)((a4 << 4) | a4);

                    int shift = (y * 4 + x) * 2;
                    uint8_t idx = (bits >> shift) & 3;

                    uint32_t dst_idx = (py * width + px) * 4;
                    dst[dst_idx + 0] = r[idx];
                    dst[dst_idx + 1] = g[idx];
                    dst[dst_idx + 2] = b[idx];
                    dst[dst_idx + 3] = a;
                }
            }
        }
    }
}

void DdsLoader::decompress_dxt5(const uint8_t* src, uint32_t width, uint32_t height, uint8_t* dst) {
    uint32_t blocks_x = (width + 3) / 4;
    uint32_t blocks_y = (height + 3) / 4;

    const uint8_t* block_ptr = src;

    for (uint32_t by = 0; by < blocks_y; by++) {
        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint8_t a0 = block_ptr[0];
            uint8_t a1 = block_ptr[1];
            uint64_t a_bits = *(const uint64_t*)block_ptr >> 16; // 48 bits of alpha indices
            block_ptr += 8;

            uint8_t a_table[8];
            a_table[0] = a0;
            a_table[1] = a1;
            if (a0 > a1) {
                a_table[2] = (uint8_t)((6 * a0 + 1 * a1) / 7);
                a_table[3] = (uint8_t)((5 * a0 + 2 * a1) / 7);
                a_table[4] = (uint8_t)((4 * a0 + 3 * a1) / 7);
                a_table[5] = (uint8_t)((3 * a0 + 4 * a1) / 7);
                a_table[6] = (uint8_t)((2 * a0 + 5 * a1) / 7);
                a_table[7] = (uint8_t)((1 * a0 + 6 * a1) / 7);
            } else {
                a_table[2] = (uint8_t)((4 * a0 + 1 * a1) / 5);
                a_table[3] = (uint8_t)((3 * a0 + 2 * a1) / 5);
                a_table[4] = (uint8_t)((2 * a0 + 3 * a1) / 5);
                a_table[5] = (uint8_t)((1 * a0 + 4 * a1) / 5);
                a_table[6] = 0;
                a_table[7] = 255;
            }

            uint16_t c0 = *(const uint16_t*)(block_ptr + 0);
            uint16_t c1 = *(const uint16_t*)(block_ptr + 2);
            uint32_t bits = *(const uint32_t*)(block_ptr + 4);
            block_ptr += 8;

            uint8_t r[4], g[4], b[4];
            decode_rgb565(c0, r[0], g[0], b[0]);
            decode_rgb565(c1, r[1], g[1], b[1]);

            r[2] = (uint8_t)((2 * r[0] + r[1]) / 3);
            g[2] = (uint8_t)((2 * g[0] + g[1]) / 3);
            b[2] = (uint8_t)((2 * b[0] + b[1]) / 3);

            r[3] = (uint8_t)((r[0] + 2 * r[1]) / 3);
            g[3] = (uint8_t)((g[0] + 2 * g[1]) / 3);
            b[3] = (uint8_t)((b[0] + 2 * b[1]) / 3);

            for (int y = 0; y < 4; y++) {
                uint32_t py = by * 4 + y;
                if (py >= height) continue;

                for (int x = 0; x < 4; x++) {
                    uint32_t px = bx * 4 + x;
                    if (px >= width) continue;

                    int a_shift = (y * 4 + x) * 3;
                    uint8_t a_idx = (uint8_t)((a_bits >> a_shift) & 7);
                    uint8_t a = a_table[a_idx];

                    int shift = (y * 4 + x) * 2;
                    uint8_t idx = (bits >> shift) & 3;

                    uint32_t dst_idx = (py * width + px) * 4;
                    dst[dst_idx + 0] = r[idx];
                    dst[dst_idx + 1] = g[idx];
                    dst[dst_idx + 2] = b[idx];
                    dst[dst_idx + 3] = a;
                }
            }
        }
    }
}

Image DdsLoader::load_image_from_memory(const uint8_t* data, size_t size, bool auto_lum_alpha) {
    Image img = {};
    if (!data || size < sizeof(DDS_HEADER)) {
        return img;
    }

    const DDS_HEADER* hdr = (const DDS_HEADER*)data;
    if (hdr->dwMagic != FOURCC('D', 'D', 'S', ' ')) {
        // Not standard magic, try Raylib internal parser first
        Image fallback = LoadImageFromMemory(".dds", data, (int)size);
        if (fallback.data) return fallback;
        return img;
    }

    uint32_t width = hdr->dwWidth;
    uint32_t height = hdr->dwHeight;
    if (width == 0 || height == 0) {
        return img;
    }

    const uint8_t* pixel_src = data + 128; // Header is 128 bytes
    size_t pixel_src_size = size - 128;

    uint8_t* rgba_data = (uint8_t*)malloc((size_t)width * height * 4);
    if (!rgba_data) return img;

    const auto& pf = hdr->ddspf;

    if (pf.dwFlags & 0x04) { // DDPF_FOURCC
        if (pf.dwFourCC == FOURCC('D', 'X', 'T', '1')) {
            decompress_dxt1(pixel_src, width, height, rgba_data);
        } else if (pf.dwFourCC == FOURCC('D', 'X', 'T', '3')) {
            decompress_dxt3(pixel_src, width, height, rgba_data);
        } else if (pf.dwFourCC == FOURCC('D', 'X', 'T', '5')) {
            decompress_dxt5(pixel_src, width, height, rgba_data);
        } else {
            // Unknown FourCC, try Raylib
            free(rgba_data);
            return LoadImageFromMemory(".dds", data, (int)size);
        }
    } else if (pf.dwFlags & 0x40) { // DDPF_RGB
        if (pf.dwRGBBitCount == 32) {
            uint32_t r_mask = pf.dwRBitMask;
            uint32_t b_mask = pf.dwBBitMask;
            uint32_t a_mask = pf.dwABitMask;
            bool is_bgra = (b_mask == 0x000000FF && r_mask == 0x00FF0000);

            const uint32_t* src32 = (const uint32_t*)pixel_src;
            size_t count = (size_t)width * height;
            for (size_t i = 0; i < count && i * 4 < pixel_src_size; i++) {
                uint32_t val = src32[i];
                if (is_bgra) {
                    rgba_data[i * 4 + 0] = (uint8_t)((val >> 16) & 0xFF);
                    rgba_data[i * 4 + 1] = (uint8_t)((val >> 8) & 0xFF);
                    rgba_data[i * 4 + 2] = (uint8_t)(val & 0xFF);
                    rgba_data[i * 4 + 3] = a_mask ? (uint8_t)((val >> 24) & 0xFF) : 255;
                } else {
                    rgba_data[i * 4 + 0] = (uint8_t)(val & 0xFF);
                    rgba_data[i * 4 + 1] = (uint8_t)((val >> 8) & 0xFF);
                    rgba_data[i * 4 + 2] = (uint8_t)((val >> 16) & 0xFF);
                    rgba_data[i * 4 + 3] = a_mask ? (uint8_t)((val >> 24) & 0xFF) : 255;
                }
            }
        } else if (pf.dwRGBBitCount == 24) {
            size_t count = (size_t)width * height;
            for (size_t i = 0; i < count && i * 3 + 2 < pixel_src_size; i++) {
                rgba_data[i * 4 + 0] = pixel_src[i * 3 + 2]; // R (assuming BGR)
                rgba_data[i * 4 + 1] = pixel_src[i * 3 + 1]; // G
                rgba_data[i * 4 + 2] = pixel_src[i * 3 + 0]; // B
                rgba_data[i * 4 + 3] = 255;
            }
        } else if (pf.dwRGBBitCount == 16) {
            uint32_t a_mask = pf.dwABitMask;
            const uint16_t* src16 = (const uint16_t*)pixel_src;
            size_t count = (size_t)width * height;
            for (size_t i = 0; i < count && i * 2 < pixel_src_size; i++) {
                uint16_t val = src16[i];
                if (a_mask == 0x8000) { // ARGB1555
                    rgba_data[i * 4 + 0] = (uint8_t)(((val >> 10) & 0x1F) * 255 / 31);
                    rgba_data[i * 4 + 1] = (uint8_t)(((val >> 5) & 0x1F) * 255 / 31);
                    rgba_data[i * 4 + 2] = (uint8_t)((val & 0x1F) * 255 / 31);
                    rgba_data[i * 4 + 3] = (val & 0x8000) ? 255 : 0;
                } else if (a_mask == 0xF000) { // ARGB4444
                    rgba_data[i * 4 + 0] = (uint8_t)(((val >> 8) & 0x0F) * 255 / 15);
                    rgba_data[i * 4 + 1] = (uint8_t)(((val >> 4) & 0x0F) * 255 / 15);
                    rgba_data[i * 4 + 2] = (uint8_t)((val & 0x0F) * 255 / 15);
                    rgba_data[i * 4 + 3] = (uint8_t)(((val >> 12) & 0x0F) * 255 / 15);
                } else { // RGB565 (default 16-bit)
                    rgba_data[i * 4 + 0] = (uint8_t)(((val >> 11) & 0x1F) * 255 / 31);
                    rgba_data[i * 4 + 1] = (uint8_t)(((val >> 5) & 0x3F) * 255 / 63);
                    rgba_data[i * 4 + 2] = (uint8_t)((val & 0x1F) * 255 / 31);
                    rgba_data[i * 4 + 3] = 255;
                }
            }
        } else {
            free(rgba_data);
            return LoadImageFromMemory(".dds", data, (int)size);
        }
    } else {
        free(rgba_data);
        return LoadImageFromMemory(".dds", data, (int)size);
    }

    // Auto calculate alpha from luminance for additive / light shaft textures
    if (auto_lum_alpha) {
        size_t total = (size_t)width * height;
        for (size_t i = 0; i < total; i++) {
            uint8_t r = rgba_data[i * 4 + 0];
            uint8_t g = rgba_data[i * 4 + 1];
            uint8_t b = rgba_data[i * 4 + 2];
            uint8_t lum = (uint8_t)((r * 299 + g * 587 + b * 114) / 1000);
            rgba_data[i * 4 + 3] = lum;
        }
    }

    img.data = rgba_data;
    img.width = (int)width;
    img.height = (int)height;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    return img;
}

Texture2D DdsLoader::load_texture_from_memory(const uint8_t* data, size_t size, bool auto_lum_alpha) {
    Image img = load_image_from_memory(data, size, auto_lum_alpha);
    if (!img.data) {
        return Texture2D{ 0, 0, 0, 0, 0 };
    }
    if (!IsWindowReady()) {
        Texture2D tex = { 1, img.width, img.height, 1, (int)img.format };
        UnloadImage(img);
        return tex;
    }
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
    return tex;
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_dds_load_texture(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);
    bool auto_lum = lua_toboolean(L, 2) != 0;

    Texture2D tex = DdsLoader::load_texture_from_memory((const uint8_t*)bytes, len, auto_lum);
    if (tex.id == 0) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to decode DDS texture");
        return 2;
    }

    lua_pushinteger(L, tex.id);
    lua_pushinteger(L, tex.width);
    lua_pushinteger(L, tex.height);
    return 3;
}

static int l_dds_unload_texture(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    if (id > 0 && IsWindowReady()) {
        Texture2D tex = {};
        tex.id = (unsigned int)id;
        UnloadTexture(tex);
    }
    return 0;
}

void register_dds_lua(lua_State* L) {
    lua_getglobal(L, "ys");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "ys");
        lua_getglobal(L, "ys");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_dds_load_texture);
    lua_setfield(L, -2, "load_texture");
    lua_pushcfunction(L, l_dds_unload_texture);
    lua_setfield(L, -2, "unload_texture");

    lua_setfield(L, -2, "dds");
    lua_pop(L, 1); // pop "ys"
}

} // namespace falcom
