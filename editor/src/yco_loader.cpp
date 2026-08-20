// yco_loader.cpp — Falcom YCO collision parser & GPU mesh builder implementation
#include "yco_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <rlgl.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace falcom {

ParsedYcoModel::ParsedYcoModel()
    : collision_type("walkable"), total_triangles(0), is_loaded(false) {
    memset(&raylib_mesh, 0, sizeof(Mesh));
    if (IsWindowReady()) {
        raylib_material = LoadMaterialDefault();
    } else {
        memset(&raylib_material, 0, sizeof(Material));
    }
    bounds = { { 0, 0, 0 }, { 0, 0, 0 } };
    center = { 0, 0, 0 };
    radius = 1.0f;
}

ParsedYcoModel::~ParsedYcoModel() {
    if (is_loaded) {
        if (IsWindowReady()) {
            UnloadMesh(raylib_mesh);
        }
        is_loaded = false;
    }
}

std::shared_ptr<ParsedYcoModel> YcoLoader::load_from_file(const std::string& path, const std::string& type_hint) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 28) {
        fclose(f);
        return nullptr;
    }
    std::vector<uint8_t> buf(sz);
    if (fread(buf.data(), 1, sz, f) != (size_t)sz) {
        fclose(f);
        return nullptr;
    }
    fclose(f);

    size_t last_slash = path.find_last_of("/\\");
    std::string fname = (last_slash != std::string::npos) ? path.substr(last_slash + 1) : path;
    return load_from_memory(buf.data(), buf.size(), type_hint, fname);
}

std::shared_ptr<ParsedYcoModel> YcoLoader::load_from_memory(const uint8_t* data, size_t size, const std::string& type_hint, const std::string& filename) {
    if (!data || size < 28) return nullptr;

    auto model = std::make_shared<ParsedYcoModel>();
    model->filename = filename;

    // Detect type from filename if not explicitly provided
    std::string fname_lower = filename;
    for (char& c : fname_lower) c = (char)tolower((unsigned char)c);

    if (fname_lower.find("__s") != std::string::npos) {
        model->collision_type = "walkable";
    } else if (fname_lower.find("__w") != std::string::npos) {
        model->collision_type = "wall";
    } else if (fname_lower.find("__c") != std::string::npos) {
        model->collision_type = "camera";
    } else {
        model->collision_type = type_hint.empty() ? "walkable" : type_hint;
    }

    uint32_t poly_count = *(const uint32_t*)(data + 0x18);
    if (poly_count == 0 || 28 + (size_t)poly_count * 96 > size) {
        // Fallback: estimate from file size
        poly_count = (uint32_t)((size - 28) / 96);
    }

    if (poly_count == 0) return nullptr;

    const size_t POLY_RECORD_SIZE = 96;
    model->polygons.reserve(poly_count);

    Vector3 bmin = { 1e9f, 1e9f, 1e9f };
    Vector3 bmax = { -1e9f, -1e9f, -1e9f };

    for (uint32_t i = 0; i < poly_count; i++) {
        size_t off = 28 + (size_t)i * POLY_RECORD_SIZE;
        if (off + POLY_RECORD_SIZE > size) break;

        const float* fptr = (const float*)(data + off);
        Vector3 v0 = { fptr[0], fptr[1], fptr[2] };
        Vector3 v1 = { fptr[3], fptr[4], fptr[5] };
        Vector3 v2 = { fptr[6], fptr[7], fptr[8] };
        Vector3 norm = { fptr[9], fptr[10], fptr[11] };
        float plane_d = fptr[12];
        uint32_t sflags = *(const uint32_t*)(data + off + 0x48);
        uint32_t cattr = *(const uint32_t*)(data + off + 0x4C);

        CollisionPoly poly;
        poly.v0 = v0; poly.v1 = v1; poly.v2 = v2;
        poly.normal = norm;
        poly.plane_d = plane_d;
        poly.surface_flags = sflags;
        poly.collision_attr = cattr;

        model->polygons.push_back(poly);

        for (const Vector3& v : { v0, v1, v2 }) {
            bmin.x = std::min(bmin.x, v.x);
            bmin.y = std::min(bmin.y, v.y);
            bmin.z = std::min(bmin.z, v.z);

            bmax.x = std::max(bmax.x, v.x);
            bmax.y = std::max(bmax.y, v.y);
            bmax.z = std::max(bmax.z, v.z);
        }
    }

    if (model->polygons.empty()) return nullptr;

    int tri_count = (int)model->polygons.size();
    int v_count = tri_count * 3;

    Mesh& mesh = model->raylib_mesh;
    mesh.triangleCount = tri_count;
    mesh.vertexCount = v_count;
    mesh.vertices = (float*)RL_MALLOC(v_count * 3 * sizeof(float));
    mesh.normals = (float*)RL_MALLOC(v_count * 3 * sizeof(float));
    mesh.colors = (unsigned char*)RL_MALLOC(v_count * 4 * sizeof(unsigned char));

    // Choose base color by type
    Color base_color = { 26, 230, 102, 120 }; // Walkable: green
    if (model->collision_type == "wall") {
        base_color = { 255, 102, 26, 120 }; // Wall: coral orange
    } else if (model->collision_type == "camera") {
        base_color = { 26, 153, 255, 120 }; // Camera: cyan
    }

    for (int i = 0; i < tri_count; i++) {
        const auto& p = model->polygons[i];
        Vector3 verts[3] = { p.v0, p.v1, p.v2 };

        for (int c = 0; c < 3; c++) {
            int out_idx = i * 3 + c;
            mesh.vertices[out_idx * 3 + 0] = verts[c].x;
            mesh.vertices[out_idx * 3 + 1] = verts[c].y;
            mesh.vertices[out_idx * 3 + 2] = verts[c].z;

            mesh.normals[out_idx * 3 + 0] = p.normal.x;
            mesh.normals[out_idx * 3 + 1] = p.normal.y;
            mesh.normals[out_idx * 3 + 2] = p.normal.z;

            mesh.colors[out_idx * 4 + 0] = base_color.r;
            mesh.colors[out_idx * 4 + 1] = base_color.g;
            mesh.colors[out_idx * 4 + 2] = base_color.b;
            mesh.colors[out_idx * 4 + 3] = base_color.a;
        }
    }

    if (IsWindowReady()) {
        UploadMesh(&mesh, false);
    }

    if (bmin.x > bmax.x) {
        bmin = { -1, -1, -1 };
        bmax = { 1, 1, 1 };
    }

    model->bounds.min = bmin;
    model->bounds.max = bmax;
    model->center = Vector3Scale(Vector3Add(bmin, bmax), 0.5f);
    model->radius = Vector3Length(Vector3Subtract(bmax, model->center));
    model->total_triangles = (uint32_t)tri_count;
    model->is_loaded = true;

    return model;
}

void YcoLoader::draw(ParsedYcoModel& model, Vector3 pos, Vector3 rot_rad, Vector3 scale, Color color, bool wireframe) {
    if (!model.is_loaded || model.raylib_mesh.vertexCount == 0) return;

    Matrix mat_t = MatrixTranslate(pos.x, pos.y, pos.z);
    Matrix mat_rx = MatrixRotateX(rot_rad.x);
    Matrix mat_ry = MatrixRotateY(rot_rad.y);
    Matrix mat_rz = MatrixRotateZ(rot_rad.z);
    Matrix mat_r = MatrixMultiply(MatrixMultiply(mat_rz, mat_rx), mat_ry);
    Matrix mat_s = MatrixScale(scale.x, scale.y, scale.z);
    Matrix transform = MatrixMultiply(MatrixMultiply(mat_s, mat_r), mat_t);
    Material mat = model.raylib_material;
    mat.maps[MATERIAL_MAP_DIFFUSE].color = color;

    // Normal offset overlay to avoid z-fighting with textures
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloat(transform));

    if (wireframe) {
        rlEnableWireMode();
        DrawMesh(model.raylib_mesh, mat, MatrixIdentity());
        rlDisableWireMode();
    } else {
        // Draw translucent face fill
        DrawMesh(model.raylib_mesh, mat, MatrixIdentity());
        // Also draw subtle wireframe edges for definition
        Color edge_col = color;
        edge_col.a = (unsigned char)std::min(255, (int)color.a + 100);
        Material edge_mat = mat;
        edge_mat.maps[MATERIAL_MAP_DIFFUSE].color = edge_col;
        rlEnableWireMode();
        DrawMesh(model.raylib_mesh, edge_mat, MatrixIdentity());
        rlDisableWireMode();
    }
    rlPopMatrix();
}

// ── YcoRegistry ─────────────────────────────────────────────────────────────

YcoRegistry& YcoRegistry::instance() {
    static YcoRegistry s_inst;
    return s_inst;
}

int YcoRegistry::register_collision(std::shared_ptr<ParsedYcoModel> model) {
    if (!model) return -1;
    int h = m_next_handle++;
    m_collisions[h] = model;
    return h;
}

ParsedYcoModel* YcoRegistry::get_collision(int handle) {
    auto it = m_collisions.find(handle);
    if (it != m_collisions.end()) return it->second.get();
    return nullptr;
}

void YcoRegistry::unregister_collision(int handle) {
    m_collisions.erase(handle);
}

void YcoRegistry::clear_all() {
    m_collisions.clear();
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_yco_load_from_memory(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);
    const char* type_hint = luaL_optstring(L, 2, "walkable");
    const char* fname = luaL_optstring(L, 3, "collision.yco");

    auto model = YcoLoader::load_from_memory((const uint8_t*)bytes, len, type_hint, fname);
    if (!model) {
        lua_pushnil(L);
        lua_pushstring(L, "Failed to parse YCO collision data");
        return 2;
    }

    int handle = YcoRegistry::instance().register_collision(model);
    lua_pushinteger(L, handle);

    lua_newtable(L);
    lua_pushstring(L, model->filename.c_str()); lua_setfield(L, -2, "filename");
    lua_pushstring(L, model->collision_type.c_str()); lua_setfield(L, -2, "type");
    lua_pushinteger(L, model->total_triangles); lua_setfield(L, -2, "total_triangles");

    lua_newtable(L);
    lua_pushnumber(L, model->bounds.min.x); lua_setfield(L, -2, "min_x");
    lua_pushnumber(L, model->bounds.min.y); lua_setfield(L, -2, "min_y");
    lua_pushnumber(L, model->bounds.min.z); lua_setfield(L, -2, "min_z");
    lua_pushnumber(L, model->bounds.max.x); lua_setfield(L, -2, "max_x");
    lua_pushnumber(L, model->bounds.max.y); lua_setfield(L, -2, "max_y");
    lua_pushnumber(L, model->bounds.max.z); lua_setfield(L, -2, "max_z");
    lua_pushnumber(L, model->center.x); lua_setfield(L, -2, "center_x");
    lua_pushnumber(L, model->center.y); lua_setfield(L, -2, "center_y");
    lua_pushnumber(L, model->center.z); lua_setfield(L, -2, "center_z");
    lua_pushnumber(L, model->radius); lua_setfield(L, -2, "radius");
    lua_setfield(L, -2, "bounds");

    return 2;
}

static int l_yco_load_from_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    const char* type_hint = luaL_optstring(L, 2, "walkable");

    auto model = YcoLoader::load_from_file(path, type_hint);
    if (!model) {
        lua_pushnil(L);
        lua_pushfstring(L, "Failed to load YCO file: %s", path);
        return 2;
    }
    int handle = YcoRegistry::instance().register_collision(model);
    lua_pushinteger(L, handle);
    return 1;
}

static int l_yco_draw(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    ParsedYcoModel* model = YcoRegistry::instance().get_collision(h);
    if (!model) return 0;

    Vector3 pos = { (float)luaL_optnumber(L, 2, 0), (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0) };
    Vector3 rot = { (float)luaL_optnumber(L, 5, 0), (float)luaL_optnumber(L, 6, 0), (float)luaL_optnumber(L, 7, 0) };
    Vector3 scale = { (float)luaL_optnumber(L, 8, 1), (float)luaL_optnumber(L, 9, 1), (float)luaL_optnumber(L, 10, 1) };

    Color col = { 26, 230, 102, 120 };
    if (!lua_isnoneornil(L, 11)) {
        col.r = (unsigned char)luaL_optinteger(L, 11, col.r);
        col.g = (unsigned char)luaL_optinteger(L, 12, col.g);
        col.b = (unsigned char)luaL_optinteger(L, 13, col.b);
        col.a = (unsigned char)luaL_optinteger(L, 14, col.a);
    } else {
        if (model->collision_type == "wall") col = { 255, 102, 26, 120 };
        else if (model->collision_type == "camera") col = { 26, 153, 255, 120 };
    }

    bool wireframe = lua_toboolean(L, 15) != 0;

    YcoLoader::draw(*model, pos, rot, scale, col, wireframe);
    return 0;
}

static int l_yco_unload(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    YcoRegistry::instance().unregister_collision(h);
    return 0;
}

void register_yco_lua(lua_State* L) {
    lua_getglobal(L, "ys");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "ys");
        lua_getglobal(L, "ys");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_yco_load_from_memory);
    lua_setfield(L, -2, "load_from_memory");
    lua_pushcfunction(L, l_yco_load_from_file);
    lua_setfield(L, -2, "load_from_file");
    lua_pushcfunction(L, l_yco_draw);
    lua_setfield(L, -2, "draw");
    lua_pushcfunction(L, l_yco_unload);
    lua_setfield(L, -2, "unload");

    lua_setfield(L, -2, "yco");
    lua_pop(L, 1); // pop "ys"
}

} // namespace falcom
