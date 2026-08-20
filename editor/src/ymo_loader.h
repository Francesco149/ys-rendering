// ymo_loader.h — High-performance Falcom YMO (Ys Model Object) parser & GPU mesh builder
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

struct YmoMaterialInfo {
    int index;
    uint32_t flags;
    float alpha;
    std::string texture_path;
    std::string texture_name;
    Texture2D texture; // Bound Raylib texture (or default white/checkers)
    bool has_custom_texture;
};

struct YmoSubmeshInfo {
    int submesh_index;
    int material_index;
    uint32_t triangle_count;
    uint32_t vertex_start;
    uint32_t vertex_count;
    int raylib_mesh_index; // Index into Model.meshes
};

struct YmoNodeInfo {
    std::string name;
    std::string parent_name;
    Matrix matrix;
};

struct ParsedYmoModel {
    std::string filename;
    uint32_t version;
    std::vector<YmoMaterialInfo> materials;
    std::vector<YmoNodeInfo> nodes;
    std::vector<YmoSubmeshInfo> submeshes;
    std::vector<std::string> collision_files;

    Model raylib_model;
    BoundingBox bounds;
    Vector3 center;
    float radius;
    uint32_t total_vertices;
    uint32_t total_triangles;
    bool is_loaded;

    ParsedYmoModel();
    ~ParsedYmoModel();
};

class YmoLoader {
public:
    static std::shared_ptr<ParsedYmoModel> load_from_memory(const uint8_t* data, size_t size, const std::string& filename = "model.ymo");
    static std::shared_ptr<ParsedYmoModel> load_from_file(const std::string& path);

    // Draws model with transform, tint, wireframe, and untextured options
    static void draw_model(ParsedYmoModel& model, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color tint, bool wireframe = false, bool untextured = false);

    // Draws single submesh
    static void draw_submesh(ParsedYmoModel& model, int submesh_idx, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color tint, bool wireframe = false, bool untextured = false);

    // Binds a Texture2D to a material index
    static void bind_material_texture(ParsedYmoModel& model, int mat_idx, Texture2D texture);

    // Initializes GLSL 330 custom shader program once upfront
    static void init_shaders();
};

// Model registry for Lua handles
class YmoRegistry {
public:
    static YmoRegistry& instance();
    int register_model(std::shared_ptr<ParsedYmoModel> model);
    ParsedYmoModel* get_model(int handle);
    void unregister_model(int handle);
    void clear_all();

private:
    YmoRegistry() : m_next_handle(1) {}
    int m_next_handle;
    std::unordered_map<int, std::shared_ptr<ParsedYmoModel>> m_models;
};

void register_ymo_lua(lua_State* L);

} // namespace falcom
