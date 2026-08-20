// sob_loader.cpp — Falcom SOB (Scene Object Placement) & SCM (Camera) parser implementation
#include "sob_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace falcom {

static bool check_door_trigger(const std::string& path, const std::string& name) {
    std::string lower = path + " " + name;
    for (char& c : lower) c = (char)tolower((unsigned char)c);

    if (lower.find("mdoor") != std::string::npos ||
        lower.find("2mdoor") != std::string::npos ||
        lower.find("tofu") != std::string::npos ||
        lower.find("3x3") != std::string::npos ||
        lower.find("check") != std::string::npos ||
        lower.find("trap") != std::string::npos ||
        lower.find("portal") != std::string::npos ||
        lower.find("trigger") != std::string::npos) {
        return true;
    }
    return false;
}

std::vector<PlacedObjectInfo> SobLoader::parse_from_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 16) {
        fclose(f);
        return {};
    }
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) {
        fclose(f);
        return {};
    }
    fclose(f);
    return parse_from_memory(buf.data(), buf.size());
}

std::vector<PlacedObjectInfo> SobLoader::parse_from_memory(const uint8_t* data, size_t size) {
    if (!data || size < 16) return {};

    if (memcmp(data, "SOB\0", 4) != 0) {
        return {};
    }

    const uint32_t* u32 = (const uint32_t*)(data + 4);
    // uint32_t version = u32[0];
    uint32_t entry_size = u32[1];
    uint32_t object_count = u32[2];

    if (entry_size == 0) entry_size = 680;
    if (object_count == 0) return {};

    std::vector<PlacedObjectInfo> objects;
    objects.reserve(object_count);

    size_t pos = 16;
    for (uint32_t i = 0; i < object_count; i++) {
        if (pos + entry_size > size) break;

        const uint8_t* ptr = data + pos;

        char path_buf[261] = {};
        memcpy(path_buf, ptr, 260);
        path_buf[260] = '\0';
        std::string raw_path = path_buf;
        size_t null_idx = raw_path.find('\0');
        if (null_idx != std::string::npos) raw_path = raw_path.substr(0, null_idx);
        while (!raw_path.empty() && (raw_path.back() == ' ' || raw_path.back() == '\r' || raw_path.back() == '\n')) raw_path.pop_back();

        std::string fname;
        size_t last_s = raw_path.find_last_of("/\\");
        if (last_s != std::string::npos) fname = raw_path.substr(last_s + 1);
        else fname = raw_path;

        std::string cname = fname;
        size_t dot = cname.rfind('.');
        if (dot != std::string::npos) cname = cname.substr(0, dot);

        // Pos at 0x104, rot at 0x110, scale at 0x11C
        const float* pos_f = (const float*)(ptr + 0x104);
        const float* rot_f = (const float*)(ptr + 0x110);
        const float* scl_f = (const float*)(ptr + 0x11C);

        float sx = (scl_f[0] > 0.0001f) ? scl_f[0] : 1.0f;
        float sy = (scl_f[1] > 0.0001f) ? scl_f[1] : 1.0f;
        float sz = (scl_f[2] > 0.0001f) ? scl_f[2] : 1.0f;

        PlacedObjectInfo obj;
        obj.index = (int)i;
        obj.model_path = raw_path;
        obj.model_filename = fname;
        obj.clean_name = cname;
        obj.position = { pos_f[0], pos_f[1], pos_f[2] };
        obj.rotation = { rot_f[0], rot_f[1], rot_f[2] };
        obj.scale = { sx, sy, sz };
        obj.is_door_trigger = check_door_trigger(raw_path, fname);

        objects.push_back(obj);
        pos += entry_size;
    }

    return objects;
}

// ── SCM Loader ──────────────────────────────────────────────────────────────

ScmCameraInfo ScmLoader::parse_from_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return { {0,0,0}, {0,0,0}, 0.79f, false, false };
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 28) {
        fclose(f);
        return { {0,0,0}, {0,0,0}, 0.79f, false, false };
    }
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) {
        fclose(f);
        return { {0,0,0}, {0,0,0}, 0.79f, false, false };
    }
    fclose(f);
    return parse_from_memory(buf.data(), buf.size());
}

ScmCameraInfo ScmLoader::parse_from_memory(const uint8_t* data, size_t size) {
    ScmCameraInfo info = {};
    if (!data || size < 24) {
        info.valid = false;
        info.pitch = 0.79f; // default ~45 deg
        return info;
    }

    const float* fptr = (const float*)data;
    // Format: max_x, max_y, max_z, min_x, min_y, min_z, pitch
    float max_x = fptr[0], max_y = fptr[1], max_z = fptr[2];
    float min_x = fptr[3], min_y = fptr[4], min_z = fptr[5];

    float pitch = 0.79f;
    if (size >= 28) {
        pitch = fptr[6];
        if (std::isnan(pitch) || pitch < -3.14f || pitch > 3.14f) {
            pitch = 0.79f;
        }
    }

    info.aabb_min = { min_x, min_y, min_z };
    info.aabb_max = { max_x, max_y, max_z };
    info.pitch = pitch;
    info.is_topdown = (fabsf(pitch) < 0.15f);
    info.valid = true;

    return info;
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_sob_parse_from_memory(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);

    auto objs = SobLoader::parse_from_memory((const uint8_t*)bytes, len);
    lua_newtable(L);

    for (size_t i = 0; i < objs.size(); i++) {
        const auto& obj = objs[i];
        lua_newtable(L);
        lua_pushinteger(L, obj.index); lua_setfield(L, -2, "index");
        lua_pushstring(L, obj.model_path.c_str()); lua_setfield(L, -2, "model_path");
        lua_pushstring(L, obj.model_filename.c_str()); lua_setfield(L, -2, "filename");
        lua_pushstring(L, obj.clean_name.c_str()); lua_setfield(L, -2, "name");
        lua_pushboolean(L, obj.is_door_trigger ? 1 : 0); lua_setfield(L, -2, "is_door_trigger");

        // Pos
        lua_newtable(L);
        lua_pushnumber(L, obj.position.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.position.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.position.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "pos");

        // Rot
        lua_newtable(L);
        lua_pushnumber(L, obj.rotation.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.rotation.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.rotation.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "rot");

        // Scale
        lua_newtable(L);
        lua_pushnumber(L, obj.scale.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.scale.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.scale.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "scale");

        lua_rawseti(L, -2, (int)i + 1);
    }

    return 1;
}

static int l_sob_parse_from_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    auto objs = SobLoader::parse_from_file(path);
    lua_newtable(L);
    for (size_t i = 0; i < objs.size(); i++) {
        const auto& obj = objs[i];
        lua_newtable(L);
        lua_pushinteger(L, obj.index); lua_setfield(L, -2, "index");
        lua_pushstring(L, obj.model_path.c_str()); lua_setfield(L, -2, "model_path");
        lua_pushstring(L, obj.model_filename.c_str()); lua_setfield(L, -2, "filename");
        lua_pushstring(L, obj.clean_name.c_str()); lua_setfield(L, -2, "name");
        lua_pushboolean(L, obj.is_door_trigger ? 1 : 0); lua_setfield(L, -2, "is_door_trigger");

        lua_newtable(L);
        lua_pushnumber(L, obj.position.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.position.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.position.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "pos");

        lua_newtable(L);
        lua_pushnumber(L, obj.rotation.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.rotation.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.rotation.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "rot");

        lua_newtable(L);
        lua_pushnumber(L, obj.scale.x); lua_setfield(L, -2, "x");
        lua_pushnumber(L, obj.scale.y); lua_setfield(L, -2, "y");
        lua_pushnumber(L, obj.scale.z); lua_setfield(L, -2, "z");
        lua_setfield(L, -2, "scale");

        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

static int l_scm_parse_from_memory(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);

    auto info = ScmLoader::parse_from_memory((const uint8_t*)bytes, len);
    lua_newtable(L);
    lua_pushboolean(L, info.valid ? 1 : 0); lua_setfield(L, -2, "valid");
    lua_pushnumber(L, info.pitch); lua_setfield(L, -2, "pitch");
    lua_pushboolean(L, info.is_topdown ? 1 : 0); lua_setfield(L, -2, "is_topdown");

    lua_newtable(L);
    lua_pushnumber(L, info.aabb_min.x); lua_setfield(L, -2, "min_x");
    lua_pushnumber(L, info.aabb_min.y); lua_setfield(L, -2, "min_y");
    lua_pushnumber(L, info.aabb_min.z); lua_setfield(L, -2, "min_z");
    lua_pushnumber(L, info.aabb_max.x); lua_setfield(L, -2, "max_x");
    lua_pushnumber(L, info.aabb_max.y); lua_setfield(L, -2, "max_y");
    lua_pushnumber(L, info.aabb_max.z); lua_setfield(L, -2, "max_z");
    lua_setfield(L, -2, "bounds");

    return 1;
}

void register_sob_scm_lua(lua_State* L) {
    lua_getglobal(L, "ys");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "ys");
        lua_getglobal(L, "ys");
    }

    // SOB table
    lua_newtable(L);
    lua_pushcfunction(L, l_sob_parse_from_memory);
    lua_setfield(L, -2, "parse_from_memory");
    lua_pushcfunction(L, l_sob_parse_from_file);
    lua_setfield(L, -2, "parse_from_file");
    lua_setfield(L, -2, "sob");

    // SCM table
    lua_newtable(L);
    lua_pushcfunction(L, l_scm_parse_from_memory);
    lua_setfield(L, -2, "parse_from_memory");
    lua_setfield(L, -2, "scm");

    lua_pop(L, 1); // pop "ys"
}

} // namespace falcom
