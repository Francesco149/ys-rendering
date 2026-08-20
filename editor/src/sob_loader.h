// sob_loader.h — Falcom SOB (Scene Object Placement) & SCM (Camera) parser
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <raylib.h>

struct lua_State;

namespace falcom {

struct PlacedObjectInfo {
    int index;
    std::string model_path;      // e.g. "data\map\mapobj\s01dor30\s01dor30.ymo"
    std::string model_filename;  // e.g. "s01dor30.ymo"
    std::string clean_name;      // e.g. "s01dor30"
    Vector3 position;
    Vector3 rotation;            // radians
    Vector3 scale;
    bool is_door_trigger;
};

struct ScmCameraInfo {
    Vector3 aabb_min;
    Vector3 aabb_max;
    float pitch;                 // radians (e.g. 0.79 rad ~45°, 0.0 top-down)
    bool is_topdown;             // true if pitch < 0.2 rad
    bool valid;
};

class SobLoader {
public:
    static std::vector<PlacedObjectInfo> parse_from_memory(const uint8_t* data, size_t size);
    static std::vector<PlacedObjectInfo> parse_from_file(const std::string& path);
};

class ScmLoader {
public:
    static ScmCameraInfo parse_from_memory(const uint8_t* data, size_t size);
    static ScmCameraInfo parse_from_file(const std::string& path);
};

void register_sob_scm_lua(lua_State* L);

} // namespace falcom
