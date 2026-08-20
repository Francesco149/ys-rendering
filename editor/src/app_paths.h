// app_paths.h — Multi-tier asset resolution, user data/config paths, and filesystem utilities
#pragma once

#include <string>
#include <vector>

struct lua_State;

#ifndef APP_NAME
#define APP_NAME "ys-viewer"
#endif

#ifndef APP_DISPLAY_NAME
#define APP_DISPLAY_NAME "Ys Map & Mesh Viewer"
#endif

namespace app_paths {

// Multi-tier asset resolution:
// 1. Environment variable override (e.g. FONT_LATIN, FONT_CJK, CUBEFORGE_ASSETS_DIR)
// 2. Application executable directory (<exe_dir>/assets/...)
// 3. Current working directory (<cwd>/assets/..., <cwd>/editor/assets/...)
// 4. Linux FHS install path (<exe_dir>/../share/cubeforge/assets/...)
// 5. User data directory (~/.local/share/cubeforge/assets/... or %LOCALAPPDATA%\cubeforge\assets\...)
// 6. System install directory (/usr/share/cubeforge/assets/..., /usr/local/share/cubeforge/assets/...)
std::string resolve_asset(const char* rel_path, const char* env_override = nullptr);

// Resolve Lua scripts root directory (checks executable dir, cwd, user profile, system share)
std::string resolve_lua_dir(const char* root_override = nullptr);

// Platform user directories (ensured to exist upon request):
std::string get_app_name();
std::string get_app_title();
void set_app_name(const char* name, const char* title = nullptr);

std::string get_user_config_dir(const char* app_name = nullptr);
std::string get_user_data_dir(const char* app_name = nullptr);
std::string get_user_documents_dir(const char* app_name = nullptr);
std::string get_user_projects_dir(const char* app_name = nullptr);

// Filesystem helpers
bool ensure_dir(const std::string& path);
bool file_exists(const std::string& path);
bool dir_exists(const std::string& path);
bool write_text_file(const std::string& path, const std::string& content);
std::string read_text_file(const std::string& path);

// Register app paths and user storage bindings to Lua (lp.app.*)
void register_lua_bindings(lua_State* L, const char* backend_name);

} // namespace app_paths
