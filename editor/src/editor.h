// editor.h — Shared C++ header for Ys Map & Mesh Viewer
#pragma once

#include <raylib.h>
#include <raymath.h>
#include <imgui.h>
#include <rlImGui.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "app_paths.h"
#include "editor_theme.h"
#include "falcom_archive.h"
#include "dds_loader.h"
#include "ymo_loader.h"
#include "yco_loader.h"
#include "sob_loader.h"
#include "async_loader.h"
// ImGui balance checker
void ig_balance_check();

// Register ImGui bindings
void ig_register(lua_State* L);

// Drive input injection state
extern bool g_drive_active;
extern float g_drive_mx;
extern float g_drive_my;
extern bool g_drive_btn[3];
extern bool g_drive_btn_pressed[3];
extern float g_drive_wheel;
extern bool g_drive_keys[512];
extern bool g_drive_key_pressed[512];

void drive_init();
void drive_step();
void drive_frame_boundary();
