// main.cpp — Ys Map & Mesh Viewer entry point
// Raylib for windowing & 3D rendering, rlImGui for ImGui overlay, Lua 5.4 for app logic & UI
#include "editor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <rlgl.h>


using namespace falcom;
// ── Global State ────────────────────────────────────────────────────────────

static lua_State* L = nullptr;
static Camera3D g_camera = {};
static float g_own_dt = 1.0f / 60.0f;
static auto g_last_frame_time = std::chrono::steady_clock::now();

static std::string g_shot_path;
static int g_shot_frames = 20;
static std::string g_drive_path;
static bool g_test_mode = false;
static bool g_hidden_window = false;

// Drive input injection state
bool g_drive_active = false;
float g_drive_mx = 0;
float g_drive_my = 0;
bool g_drive_btn[3] = { false, false, false };
bool g_drive_btn_pressed[3] = { false, false, false };
float g_drive_wheel = 0;
bool g_drive_keys[512] = {};
bool g_drive_key_pressed[512] = {};

static float g_prev_drive_mx = 0;
static float g_prev_drive_my = 0;

// Render texture registry for 3D thumbnails & offscreen viewports
struct RTEntry {
    RenderTexture2D rt;
    Camera3D cam;
};
static std::unordered_map<int, RTEntry> g_render_textures;
static int g_next_rt_id = 1;

// ── Helpers ─────────────────────────────────────────────────────────────────

static void update_own_dt() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<float> elapsed = now - g_last_frame_time;
    g_last_frame_time = now;
    g_own_dt = std::min(0.1f, std::max(0.0001f, elapsed.count()));
}

// ── Input & Drive ───────────────────────────────────────────────────────────

void drive_init() {
    g_drive_active = false;
    g_drive_mx = 0; g_drive_my = 0;
    g_prev_drive_mx = 0; g_prev_drive_my = 0;
    memset(g_drive_btn, 0, sizeof(g_drive_btn));
    memset(g_drive_btn_pressed, 0, sizeof(g_drive_btn_pressed));
    g_drive_wheel = 0;
    memset(g_drive_keys, 0, sizeof(g_drive_keys));
    memset(g_drive_key_pressed, 0, sizeof(g_drive_key_pressed));
}

void drive_step() {
    if (!g_drive_active || !L) return;
    lua_getglobal(L, "drive_step");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[drive] drive_step error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

void drive_frame_boundary() {
    g_prev_drive_mx = g_drive_mx;
    g_prev_drive_my = g_drive_my;
    memset(g_drive_btn_pressed, 0, sizeof(g_drive_btn_pressed));
    memset(g_drive_key_pressed, 0, sizeof(g_drive_key_pressed));
    g_drive_wheel = 0;

    if (!g_drive_active || !L) return;
    lua_getglobal(L, "drive_frame");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[drive] drive_frame error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
}

// ── Lua lp.rl.* Bindings ───────────────────────────────────────────────────

static int l_rl_set_camera(lua_State* L) {
    g_camera.position = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    g_camera.target = { (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6) };
    g_camera.up = { 0.0f, 1.0f, 0.0f };
    g_camera.fovy = (float)luaL_optnumber(L, 7, 45.0);
    g_camera.projection = CAMERA_PERSPECTIVE;
    return 0;
}

static int l_rl_get_camera(lua_State* L) {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushnumber(L, g_camera.position.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, g_camera.position.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, g_camera.position.z); lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "eye");

    lua_newtable(L);
    lua_pushnumber(L, g_camera.target.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, g_camera.target.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, g_camera.target.z); lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "target");

    lua_pushnumber(L, g_camera.fovy);
    lua_setfield(L, -2, "fovy");
    return 1;
}

static int l_rl_draw_grid(lua_State* L) {
    int slices = (int)luaL_optinteger(L, 1, 40);
    float spacing = (float)luaL_optnumber(L, 2, 2.0f);
    int alpha = (int)luaL_optinteger(L, 6, 255);
    if (lua_gettop(L) >= 5) {
        float cx = (float)luaL_checknumber(L, 3);
        float cy = (float)luaL_checknumber(L, 4);
        float cz = (float)luaL_checknumber(L, 5);
        rlPushMatrix();
        rlTranslatef(cx, cy, cz);
        if (alpha < 255) {
            rlSetBlendMode(BLEND_ALPHA);
            rlColor4ub(100, 105, 120, (unsigned char)alpha);
        }
        DrawGrid(slices, spacing);
        rlPopMatrix();
    } else {
        DrawGrid(slices, spacing);
    }
    return 0;
}

static int l_rl_draw_line_3d(lua_State* L) {
    Vector3 start = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    Vector3 end = { (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5), (float)luaL_checknumber(L, 6) };
    Color c = {
        (unsigned char)luaL_optinteger(L, 7, 255),
        (unsigned char)luaL_optinteger(L, 8, 255),
        (unsigned char)luaL_optinteger(L, 9, 255),
        (unsigned char)luaL_optinteger(L, 10, 255)
    };
    DrawLine3D(start, end, c);
    return 0;
}

static int l_rl_draw_cube(lua_State* L) {
    Vector3 pos = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    float d = (float)luaL_checknumber(L, 6);
    Color c = {
        (unsigned char)luaL_optinteger(L, 7, 255),
        (unsigned char)luaL_optinteger(L, 8, 255),
        (unsigned char)luaL_optinteger(L, 9, 255),
        (unsigned char)luaL_optinteger(L, 10, 255)
    };
    DrawCube(pos, w, h, d, c);
    return 0;
}

static int l_rl_draw_cube_wires(lua_State* L) {
    Vector3 pos = { (float)luaL_checknumber(L, 1), (float)luaL_checknumber(L, 2), (float)luaL_checknumber(L, 3) };
    float w = (float)luaL_checknumber(L, 4);
    float h = (float)luaL_checknumber(L, 5);
    float d = (float)luaL_checknumber(L, 6);
    Color c = {
        (unsigned char)luaL_optinteger(L, 7, 255),
        (unsigned char)luaL_optinteger(L, 8, 255),
        (unsigned char)luaL_optinteger(L, 9, 255),
        (unsigned char)luaL_optinteger(L, 10, 255)
    };
    DrawCubeWires(pos, w, h, d, c);
    return 0;
}

static int l_rl_get_screen_size(lua_State* L) {
    lua_pushinteger(L, GetScreenWidth());
    lua_pushinteger(L, GetScreenHeight());
    return 2;
}

static int l_rl_get_frame_time(lua_State* L) {
    lua_pushnumber(L, g_own_dt);
    return 1;
}

static int l_rl_get_mouse_pos(lua_State* L) {
    if (g_drive_active) {
        lua_pushnumber(L, g_drive_mx);
        lua_pushnumber(L, g_drive_my);
    } else {
        Vector2 m = GetMousePosition();
        lua_pushnumber(L, m.x);
        lua_pushnumber(L, m.y);
    }
    return 2;
}

static int l_rl_get_mouse_delta(lua_State* L) {
    if (g_drive_active) {
        lua_pushnumber(L, g_drive_mx - g_prev_drive_mx);
        lua_pushnumber(L, g_drive_my - g_prev_drive_my);
    } else {
        Vector2 d = GetMouseDelta();
        lua_pushnumber(L, d.x);
        lua_pushnumber(L, d.y);
    }
    return 2;
}

static int l_rl_get_mouse_wheel(lua_State* L) {
    if (g_drive_active) {
        lua_pushnumber(L, g_drive_wheel);
    } else {
        lua_pushnumber(L, GetMouseWheelMove());
    }
    return 1;
}

static int l_rl_is_mouse_button_down(lua_State* L) {
    int btn = (int)luaL_checkinteger(L, 1);
    if (g_drive_active) {
        bool down = (btn >= 0 && btn < 3) ? g_drive_btn[btn] : false;
        lua_pushboolean(L, down ? 1 : 0);
    } else {
        lua_pushboolean(L, IsMouseButtonDown(btn) ? 1 : 0);
    }
    return 1;
}

static int l_rl_is_mouse_button_pressed(lua_State* L) {
    int btn = (int)luaL_checkinteger(L, 1);
    if (g_drive_active) {
        bool pr = (btn >= 0 && btn < 3) ? g_drive_btn_pressed[btn] : false;
        lua_pushboolean(L, pr ? 1 : 0);
    } else {
        lua_pushboolean(L, IsMouseButtonPressed(btn) ? 1 : 0);
    }
    return 1;
}

static int l_rl_is_key_down(lua_State* L) {
    int key = (int)luaL_checkinteger(L, 1);
    if (g_drive_active) {
        bool down = (key >= 0 && key < 512) ? g_drive_keys[key] : false;
        lua_pushboolean(L, down ? 1 : 0);
    } else {
        lua_pushboolean(L, IsKeyDown(key) ? 1 : 0);
    }
    return 1;
}

static int l_rl_is_key_pressed(lua_State* L) {
    int key = (int)luaL_checkinteger(L, 1);
    if (g_drive_active) {
        bool pr = (key >= 0 && key < 512) ? g_drive_key_pressed[key] : false;
        lua_pushboolean(L, pr ? 1 : 0);
    } else {
        lua_pushboolean(L, IsKeyPressed(key) ? 1 : 0);
    }
    return 1;
}

static int l_rl_get_char_pressed(lua_State* L) {
    int ch = GetCharPressed();
    lua_pushinteger(L, ch);
    return 1;
}

static int l_rl_set_mouse_cursor(lua_State* L) {
    int cur = (int)luaL_checkinteger(L, 1);
    SetMouseCursor(cur);
    return 0;
}

// ── Render Texture & Thumbnail Generator ───────────────────────────────────

static int l_rl_create_render_texture(lua_State* L) {
    int w = (int)luaL_checkinteger(L, 1);
    int h = (int)luaL_checkinteger(L, 2);
    RTEntry entry = {};
    if (IsWindowReady()) {
        entry.rt = LoadRenderTexture(w, h);
        SetTextureFilter(entry.rt.texture, TEXTURE_FILTER_BILINEAR);
    } else {
        entry.rt = {};
        entry.rt.texture.id = 1; // Dummy texture ID for headless testing
    }

    entry.cam.position = { 0.0f, 15.0f, 25.0f };
    entry.cam.target = { 0.0f, 0.0f, 0.0f };
    entry.cam.up = { 0.0f, 1.0f, 0.0f };
    entry.cam.fovy = 45.0f;
    entry.cam.projection = CAMERA_PERSPECTIVE;

    int id = g_next_rt_id++;
    g_render_textures[id] = entry;
    lua_pushinteger(L, id);
    return 1;
}

static int l_rl_unload_render_texture(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    auto it = g_render_textures.find(id);
    if (it != g_render_textures.end()) {
        if (IsWindowReady() && it->second.rt.id > 1) {
            UnloadRenderTexture(it->second.rt);
        }
        g_render_textures.erase(it);
    }
    return 0;
}

static int l_rl_get_render_texture_gl_id(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    auto it = g_render_textures.find(id);
    if (it != g_render_textures.end()) {
        lua_pushinteger(L, (lua_Integer)(intptr_t)it->second.rt.texture.id);
    } else {
        lua_pushinteger(L, 0);
    }
    return 1;
}

static int l_rl_render_map_thumbnail(lua_State* L) {
    int rt_id = (int)luaL_checkinteger(L, 1);
    int model_id = (int)luaL_checkinteger(L, 2);
    int coll_id = (int)luaL_optinteger(L, 3, -1);
    float rot_y_deg = (float)luaL_optnumber(L, 4, 0.0f);
    bool untextured = lua_toboolean(L, 5) != 0;

    if (!IsWindowReady()) return 0;

    auto it = g_render_textures.find(rt_id);
    if (it == g_render_textures.end()) return 0;
    ParsedYmoModel* model = YmoRegistry::instance().get_model(model_id);
    ParsedYcoModel* coll = (coll_id >= 0) ? YcoRegistry::instance().get_collision(coll_id) : nullptr;

    RTEntry& entry = it->second;

    // Position camera based on model bounds
    Vector3 center = { 0, 0, 0 };
    float rad = 20.0f;
    if (model && model->is_loaded) {
        center = model->center;
        rad = std::max(5.0f, model->radius);
    } else if (coll && coll->is_loaded) {
        center = coll->center;
        rad = std::max(5.0f, coll->radius);
    }

    float cam_dist = rad * 1.8f;
    float cam_height = rad * 1.1f;
    float rot_rad = rot_y_deg * DEG2RAD;

    Vector3 cam_pos = {
        center.x + sinf(rot_rad) * cam_dist,
        center.y + cam_height,
        center.z + cosf(rot_rad) * cam_dist
    };

    entry.cam.position = cam_pos;
    entry.cam.target = center;

    BeginTextureMode(entry.rt);
    ClearBackground({ 24, 26, 32, 255 }); // Slate dark thumbnail bg

    BeginMode3D(entry.cam);
    rlDisableBackfaceCulling();
    rlEnableDepthTest();
    float grid_y = (model && model->is_loaded) ? (model->bounds.min.y - 0.5f) : (center.y - 5.0f);
    rlPushMatrix();
    rlTranslatef(center.x, grid_y, center.z);
    DrawGrid(20, std::max(2.0f, rad / 10.0f));
    rlPopMatrix();

    // Draw Model if loaded
    if (model && model->is_loaded) {
        YmoLoader::draw_model(*model, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, WHITE, false, untextured);
    }

    // Draw Collision if model not available or requested
    if (coll && coll->is_loaded && (!model || !model->is_loaded)) {
        YcoLoader::draw(*coll, { 0, 0, 0 }, { 0, 0, 0 }, { 1, 1, 1 }, { 26, 230, 102, 180 }, false);
    }

    EndMode3D();
    EndTextureMode();
    return 0;
}

// ── Drive Lua Bindings ──────────────────────────────────────────────────────

static int l_drive_active(lua_State* L) {
    g_drive_active = lua_toboolean(L, 1) != 0;
    return 0;
}

static int l_drive_mouse(lua_State* L) {
    g_drive_mx = (float)luaL_checknumber(L, 1);
    g_drive_my = (float)luaL_checknumber(L, 2);
    return 0;
}

static int l_drive_button(lua_State* L) {
    int btn = (int)luaL_checkinteger(L, 1);
    bool down = lua_toboolean(L, 2) != 0;
    if (btn >= 0 && btn < 3) {
        if (down && !g_drive_btn[btn]) {
            g_drive_btn_pressed[btn] = true;
        }
        g_drive_btn[btn] = down;
    }
    return 0;
}

static int l_drive_wheel(lua_State* L) {
    g_drive_wheel += (float)luaL_checknumber(L, 1);
    return 0;
}

static int l_drive_key(lua_State* L) {
    int key = (int)luaL_checkinteger(L, 1);
    bool down = lua_toboolean(L, 2) != 0;
    if (key >= 0 && key < 512) {
        if (down && !g_drive_keys[key]) {
            g_drive_key_pressed[key] = true;
        }
        g_drive_keys[key] = down;
    }
    return 0;
}

// ── Registration ────────────────────────────────────────────────────────────

static void register_all_lua(lua_State* L) {
    // lp namespace
    lua_newtable(L);

    // lp.rl
    lua_newtable(L);
    lua_pushcfunction(L, l_rl_set_camera); lua_setfield(L, -2, "set_camera");
    lua_pushcfunction(L, l_rl_get_camera); lua_setfield(L, -2, "get_camera");
    lua_pushcfunction(L, l_rl_draw_grid); lua_setfield(L, -2, "draw_grid");
    lua_pushcfunction(L, l_rl_draw_line_3d); lua_setfield(L, -2, "draw_line_3d");
    lua_pushcfunction(L, l_rl_draw_cube); lua_setfield(L, -2, "draw_cube");
    lua_pushcfunction(L, l_rl_draw_cube_wires); lua_setfield(L, -2, "draw_cube_wires");
    lua_pushcfunction(L, l_rl_get_screen_size); lua_setfield(L, -2, "get_screen_size");
    lua_pushcfunction(L, l_rl_get_frame_time); lua_setfield(L, -2, "get_frame_time");
    lua_pushcfunction(L, l_rl_get_mouse_pos); lua_setfield(L, -2, "get_mouse_pos");
    lua_pushcfunction(L, l_rl_get_mouse_delta); lua_setfield(L, -2, "get_mouse_delta");
    lua_pushcfunction(L, l_rl_get_mouse_wheel); lua_setfield(L, -2, "get_mouse_wheel");
    lua_pushcfunction(L, l_rl_is_mouse_button_down); lua_setfield(L, -2, "is_mouse_button_down");
    lua_pushcfunction(L, l_rl_is_mouse_button_pressed); lua_setfield(L, -2, "is_mouse_button_pressed");
    lua_pushcfunction(L, l_rl_is_key_down); lua_setfield(L, -2, "is_key_down");
    lua_pushcfunction(L, l_rl_is_key_pressed); lua_setfield(L, -2, "is_key_pressed");
    lua_pushcfunction(L, l_rl_get_char_pressed); lua_setfield(L, -2, "get_char_pressed");
    lua_pushcfunction(L, l_rl_set_mouse_cursor); lua_setfield(L, -2, "set_mouse_cursor");
    lua_pushcfunction(L, l_rl_create_render_texture); lua_setfield(L, -2, "create_render_texture");
    lua_pushcfunction(L, l_rl_unload_render_texture); lua_setfield(L, -2, "unload_render_texture");
    lua_pushcfunction(L, l_rl_get_render_texture_gl_id); lua_setfield(L, -2, "get_render_texture_gl_id");
    lua_pushcfunction(L, l_rl_render_map_thumbnail); lua_setfield(L, -2, "render_map_thumbnail");

    // Cursor constants
    lua_pushinteger(L, MOUSE_CURSOR_DEFAULT); lua_setfield(L, -2, "CURSOR_DEFAULT");
    lua_pushinteger(L, MOUSE_CURSOR_ARROW); lua_setfield(L, -2, "CURSOR_ARROW");
    lua_pushinteger(L, MOUSE_CURSOR_IBEAM); lua_setfield(L, -2, "CURSOR_IBEAM");
    lua_pushinteger(L, MOUSE_CURSOR_CROSSHAIR); lua_setfield(L, -2, "CURSOR_CROSSHAIR");
    lua_pushinteger(L, MOUSE_CURSOR_POINTING_HAND); lua_setfield(L, -2, "CURSOR_POINTING_HAND");
    lua_pushinteger(L, MOUSE_CURSOR_RESIZE_EW); lua_setfield(L, -2, "CURSOR_RESIZE_EW");
    lua_pushinteger(L, MOUSE_CURSOR_RESIZE_NS); lua_setfield(L, -2, "CURSOR_RESIZE_NS");
    lua_pushinteger(L, MOUSE_CURSOR_RESIZE_NWSE); lua_setfield(L, -2, "CURSOR_RESIZE_NWSE");
    lua_pushinteger(L, MOUSE_CURSOR_RESIZE_NESW); lua_setfield(L, -2, "CURSOR_RESIZE_NESW");
    lua_pushinteger(L, MOUSE_CURSOR_RESIZE_ALL); lua_setfield(L, -2, "CURSOR_RESIZE_ALL");
    lua_pushinteger(L, MOUSE_CURSOR_NOT_ALLOWED); lua_setfield(L, -2, "CURSOR_NOT_ALLOWED");

    // Raylib Keys
    lua_newtable(L);
    lua_pushinteger(L, KEY_SPACE); lua_setfield(L, -2, "Space");
    lua_pushinteger(L, KEY_ESCAPE); lua_setfield(L, -2, "Escape");
    lua_pushinteger(L, KEY_ENTER); lua_setfield(L, -2, "Enter");
    lua_pushinteger(L, KEY_TAB); lua_setfield(L, -2, "Tab");
    lua_pushinteger(L, KEY_BACKSPACE); lua_setfield(L, -2, "Backspace");
    lua_pushinteger(L, KEY_DELETE); lua_setfield(L, -2, "Delete");
    lua_pushinteger(L, KEY_LEFT_SHIFT); lua_setfield(L, -2, "LeftShift");
    lua_pushinteger(L, KEY_RIGHT_SHIFT); lua_setfield(L, -2, "RightShift");
    lua_pushinteger(L, KEY_LEFT_CONTROL); lua_setfield(L, -2, "LeftCtrl");
    lua_pushinteger(L, KEY_LEFT_ALT); lua_setfield(L, -2, "LeftAlt");
    lua_pushinteger(L, KEY_A); lua_setfield(L, -2, "A");
    lua_pushinteger(L, KEY_B); lua_setfield(L, -2, "B");
    lua_pushinteger(L, KEY_C); lua_setfield(L, -2, "C");
    lua_pushinteger(L, KEY_D); lua_setfield(L, -2, "D");
    lua_pushinteger(L, KEY_E); lua_setfield(L, -2, "E");
    lua_pushinteger(L, KEY_F); lua_setfield(L, -2, "F");
    lua_pushinteger(L, KEY_G); lua_setfield(L, -2, "G");
    lua_pushinteger(L, KEY_H); lua_setfield(L, -2, "H");
    lua_pushinteger(L, KEY_I); lua_setfield(L, -2, "I");
    lua_pushinteger(L, KEY_J); lua_setfield(L, -2, "J");
    lua_pushinteger(L, KEY_K); lua_setfield(L, -2, "K");
    lua_pushinteger(L, KEY_L); lua_setfield(L, -2, "L");
    lua_pushinteger(L, KEY_M); lua_setfield(L, -2, "M");
    lua_pushinteger(L, KEY_N); lua_setfield(L, -2, "N");
    lua_pushinteger(L, KEY_O); lua_setfield(L, -2, "O");
    lua_pushinteger(L, KEY_P); lua_setfield(L, -2, "P");
    lua_pushinteger(L, KEY_Q); lua_setfield(L, -2, "Q");
    lua_pushinteger(L, KEY_R); lua_setfield(L, -2, "R");
    lua_pushinteger(L, KEY_S); lua_setfield(L, -2, "S");
    lua_pushinteger(L, KEY_T); lua_setfield(L, -2, "T");
    lua_pushinteger(L, KEY_U); lua_setfield(L, -2, "U");
    lua_pushinteger(L, KEY_V); lua_setfield(L, -2, "V");
    lua_pushinteger(L, KEY_W); lua_setfield(L, -2, "W");
    lua_pushinteger(L, KEY_X); lua_setfield(L, -2, "X");
    lua_pushinteger(L, KEY_Y); lua_setfield(L, -2, "Y");
    lua_pushinteger(L, KEY_Z); lua_setfield(L, -2, "Z");
    lua_pushinteger(L, KEY_ONE); lua_setfield(L, -2, "One");
    lua_pushinteger(L, KEY_TWO); lua_setfield(L, -2, "Two");
    lua_pushinteger(L, KEY_THREE); lua_setfield(L, -2, "Three");
    lua_pushinteger(L, KEY_FOUR); lua_setfield(L, -2, "Four");
    lua_pushinteger(L, KEY_FIVE); lua_setfield(L, -2, "Five");
    lua_setfield(L, -2, "key");

    lua_setfield(L, -2, "rl");

    // lp.drive
    lua_newtable(L);
    lua_pushcfunction(L, l_drive_active); lua_setfield(L, -2, "active");
    lua_pushcfunction(L, l_drive_mouse); lua_setfield(L, -2, "mouse");
    lua_pushcfunction(L, l_drive_button); lua_setfield(L, -2, "button");
    lua_pushcfunction(L, l_drive_wheel); lua_setfield(L, -2, "wheel");
    lua_pushcfunction(L, l_drive_key); lua_setfield(L, -2, "key");
    lua_setfield(L, -2, "drive");

    lua_setglobal(L, "lp");

    // Register other modules
    ig_register(L);
    app_paths::register_lua_bindings(L, "Raylib 6.0 + OpenGL 3.3");
    falcom::register_archive_lua(L);
    falcom::register_dds_lua(L);
    falcom::register_ymo_lua(L);
    falcom::register_yco_lua(L);
    falcom::register_sob_scm_lua(L);
    async_io::register_async_lua(L);
}

// ── Main Render Pass ────────────────────────────────────────────────────────

static void render_frame_contents() {
    update_own_dt();
    drive_step();

    BeginDrawing();
    ClearBackground({ 18, 19, 23, 255 }); // Dark viewport background

    // 3D pass
    BeginMode3D(g_camera);
    lua_getglobal(L, "lp_draw3d");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[lua] lp_draw3d error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    EndMode3D();

    // 2D pass
    lua_getglobal(L, "lp_draw2d");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[lua] lp_draw2d error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }

    // ImGui pass
    rlImGuiBeginDelta(g_own_dt);

    // Update ImGui IO with drive input when active
    if (g_drive_active) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(g_drive_mx, g_drive_my);
        io.AddMouseButtonEvent(0, g_drive_btn[0]);
        io.AddMouseButtonEvent(1, g_drive_btn[1]);
        io.AddMouseButtonEvent(2, g_drive_btn[2]);
        if (g_drive_wheel != 0) {
            io.AddMouseWheelEvent(0, g_drive_wheel);
        }
    }

    lua_getglobal(L, "lp_frame");
    if (lua_isfunction(L, -1)) {
        if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
            fprintf(stderr, "[lua] lp_frame error: %s\n", lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }

    ig_balance_check();
    rlImGuiEnd();

    drive_frame_boundary();
}

#ifdef _WIN32
extern "C" {
__declspec(dllimport) void* __stdcall wglGetCurrentDC(void);
__declspec(dllimport) int   __stdcall SwapBuffers(void*);
__declspec(dllimport) long long __stdcall CallWindowProcW(long long, void*, unsigned, unsigned long long, long long);
__declspec(dllimport) long long __stdcall GetWindowLongPtrW(void*, int);
__declspec(dllimport) long long __stdcall SetWindowLongPtrW(void*, int, long long);
}

static void present_no_poll() {
    rlDrawRenderBatchActive();
    SwapBuffers(wglGetCurrentDC());
}

#define YS_GWLP_WNDPROC      (-4)
#define YS_WM_SIZE           0x0005u
#define YS_WM_ENTERSIZEMOVE  0x0231u
#define YS_WM_EXITSIZEMOVE   0x0232u
#define YS_SIZE_MINIMIZED    1ull
typedef long long (*YsWndProc)(void*, unsigned, unsigned long long, long long);
static YsWndProc g_orig_proc = nullptr;
static bool      g_in_sizemove = false;
static bool      g_in_subclass_render = false;

static long long __stdcall ys_resize_subclass_proc(void* hwnd, unsigned msg,
                                                   unsigned long long wp, long long lp) {
    if (msg == YS_WM_ENTERSIZEMOVE) g_in_sizemove = true;
    if (msg == YS_WM_EXITSIZEMOVE)  g_in_sizemove = false;

    long long res = CallWindowProcW((long long)g_orig_proc, hwnd, msg, wp, lp);

    if (msg == YS_WM_SIZE && wp != YS_SIZE_MINIMIZED && g_in_sizemove &&
        !g_in_subclass_render && !g_hidden_window) {
        g_in_subclass_render = true;
        render_frame_contents();
        present_no_poll();
        g_in_subclass_render = false;
    }
    return res;
}

static void install_resize_subclass() {
    void* hwnd = GetWindowHandle();
    g_orig_proc = (YsWndProc)GetWindowLongPtrW(hwnd, YS_GWLP_WNDPROC);
    SetWindowLongPtrW(hwnd, YS_GWLP_WNDPROC, (long long)ys_resize_subclass_proc);
}
static void uninstall_resize_subclass() {
    if (!g_orig_proc) return;
    void* hwnd = GetWindowHandle();
    SetWindowLongPtrW(hwnd, YS_GWLP_WNDPROC, (long long)g_orig_proc);
    g_orig_proc = nullptr;
}
#endif

// ── Main Entry Point ────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // Parse CLI arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--test") == 0) {
            g_test_mode = true;
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            g_shot_path = argv[++i];
            g_hidden_window = true;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            g_shot_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
            g_drive_path = argv[++i];
        }
    }

    // Initialize Lua VM
    L = luaL_newstate();
    luaL_openlibs(L);
    register_all_lua(L);

    // Setup Lua package.path to resolve scripts
    std::string lua_dir = app_paths::resolve_lua_dir();
    std::string ppath = lua_dir + "/?.lua;" + lua_dir + "/?/init.lua;./lua/?.lua;./editor/lua/?.lua;./?.lua";
    lua_getglobal(L, "package");
    lua_pushstring(L, ppath.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    // Headless test mode (--test)
    if (g_test_mode) {
        printf("Running headless test mode...\n");
        std::string testmain = lua_dir + "/../tests/testmain.lua";
        if (!app_paths::file_exists(testmain)) testmain = "editor/tests/testmain.lua";
        if (!app_paths::file_exists(testmain)) testmain = "tests/testmain.lua";

        if (luaL_dofile(L, testmain.c_str()) != LUA_OK) {
            fprintf(stderr, "[test] FAILED to run %s: %s\n", testmain.c_str(), lua_tostring(L, -1));
            lua_close(L);
            return 1;
        }
        printf("[test] Headless test suite PASSED.\n");
        lua_close(L);
        return 0;
    }

    // Initialize Raylib Window
    if (g_hidden_window) {
        SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_MSAA_4X_HINT);
    } else {
        SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
    }

    InitWindow(1280, 800, "Ys Map & Mesh Viewer");
    SetExitKey(KEY_NULL); // Invariant: do not close app on ESC; app handles ESC navigation
    SetWindowMinSize(800, 600);
    SetTargetFPS(60);
    // Setup ImGui & Fonts & Theme
    rlImGuiBeginInitImGui();
    apply_modern_dark_theme();
    build_imgui_font_atlas();
    rlImGuiEndInitImGui();
    falcom::YmoLoader::init_shaders();
    g_camera.position = { 0.0f, 25.0f, 40.0f };
    g_camera.target = { 0.0f, 0.0f, 0.0f };
    g_camera.up = { 0.0f, 1.0f, 0.0f };
    g_camera.fovy = 45.0f;
    g_camera.projection = CAMERA_PERSPECTIVE;

    // Load main Lua script
    std::string main_script = lua_dir + "/main.lua";
    if (luaL_dofile(L, main_script.c_str()) != LUA_OK) {
        fprintf(stderr, "[lua] Failed to load %s: %s\n", main_script.c_str(), lua_tostring(L, -1));
    }

    // Load input tape if specified (--drive)
    if (!g_drive_path.empty()) {
        if (luaL_dofile(L, g_drive_path.c_str()) != LUA_OK) {
            fprintf(stderr, "[drive] Failed to load tape %s: %s\n", g_drive_path.c_str(), lua_tostring(L, -1));
        }
    }

    // Main Loop
#ifdef _WIN32
    if (!g_hidden_window && !g_test_mode) {
        install_resize_subclass();
    }
#endif

    int frame_count = 0;
    while (!WindowShouldClose()) {
        render_frame_contents();
#ifdef _WIN32
        present_no_poll();
        PollInputEvents();
#else
        EndDrawing();
#endif

        frame_count++;
        if (!g_shot_path.empty() && frame_count >= g_shot_frames) {
            TakeScreenshot(g_shot_path.c_str());
            printf("Captured screenshot to %s (after %d frames)\n", g_shot_path.c_str(), frame_count);
            break;
        }
    }

#ifdef _WIN32
    if (!g_hidden_window && !g_test_mode) {
        uninstall_resize_subclass();
    }
#endif
    for (auto& pair : g_render_textures) {
        UnloadRenderTexture(pair.second.rt);
    }
    g_render_textures.clear();

    rlImGuiShutdown();
    CloseWindow();
    lua_close(L);
    return 0;
}
