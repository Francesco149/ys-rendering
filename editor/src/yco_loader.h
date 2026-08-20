// yco_loader.h — High-performance Falcom YCO (Ys Collision Object) parser & GPU mesh builder
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <raylib.h>
#include <raymath.h>

struct lua_State;

namespace falcom {

struct CollisionPoly {
    Vector3 v0, v1, v2;
    Vector3 normal;
    float plane_d;
    uint32_t surface_flags;
    uint32_t collision_attr;
};

struct ParsedYcoModel {
    std::string filename;
    std::string collision_type; // "walkable", "wall", "camera"
    std::vector<CollisionPoly> polygons;

    Mesh raylib_mesh;
    Material raylib_material;
    BoundingBox bounds;
    Vector3 center;
    float radius;
    uint32_t total_triangles;
    bool is_loaded;

    ParsedYcoModel();
    ~ParsedYcoModel();
};

class YcoLoader {
public:
    static std::shared_ptr<ParsedYcoModel> load_from_memory(const uint8_t* data, size_t size, const std::string& type_hint = "walkable", const std::string& filename = "collision.yco");
    static std::shared_ptr<ParsedYcoModel> load_from_file(const std::string& path, const std::string& type_hint = "walkable");

    // Draws translucent collision mesh
    static void draw(ParsedYcoModel& model, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color color, bool wireframe = false);
};

// YCO registry for Lua handles
class YcoRegistry {
public:
    static YcoRegistry& instance();
    int register_collision(std::shared_ptr<ParsedYcoModel> model);
    ParsedYcoModel* get_collision(int handle);
    void unregister_collision(int handle);
    void clear_all();

private:
    YcoRegistry() : m_next_handle(1) {}
    int m_next_handle;
    std::unordered_map<int, std::shared_ptr<ParsedYcoModel>> m_collisions;
};

void register_yco_lua(lua_State* L);

} // namespace falcom
