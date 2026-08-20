// app_paths.cpp — Multi-tier asset resolution and platform user paths
#include "app_paths.h"
#include "tinyfiledialogs.h"
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <pwd.h>
#endif

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace app_paths {
static std::string s_app_name = APP_NAME;
static std::string s_app_title = APP_DISPLAY_NAME;

std::string get_app_name() { return s_app_name; }
std::string get_app_title() { return s_app_title; }
void set_app_name(const char* name, const char* title) {
    if (name && *name) s_app_name = name;
    if (title && *title) s_app_title = title;
    else if (name && *name) s_app_title = name;
}

static std::string get_exe_dir() {
    char buf[2048] = {};
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf)) {
        char* last = strrchr(buf, '\\');
        if (!last) last = strrchr(buf, '/');
        if (last) *last = '\0';
        return std::string(buf);
    }
#else
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        char* last = strrchr(buf, '/');
        if (last) *last = '\0';
        return std::string(buf);
    }
#endif
    return ".";
}

bool file_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG));
}

bool dir_exists(const std::string& path) {
    if (path.empty()) return false;
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR));
}

bool ensure_dir(const std::string& path) {
    if (path.empty()) return false;
    if (dir_exists(path)) return true;

    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", path.c_str());
    size_t len = strlen(tmp);
    if (len == 0) return false;
    if (tmp[len - 1] == '/' || tmp[len - 1] == '\\') {
        tmp[len - 1] = '\0';
    }

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char sep = *p;
            *p = '\0';
#ifdef _WIN32
            _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = sep;
        }
    }
#ifdef _WIN32
    return _mkdir(tmp) == 0 || dir_exists(tmp);
#else
    return mkdir(tmp, 0755) == 0 || dir_exists(tmp);
#endif
}

bool write_text_file(const std::string& path, const std::string& content) {
    // Ensure parent dir exists
    size_t last_sep = path.find_last_of("/\\");
    if (last_sep != std::string::npos) {
        ensure_dir(path.substr(0, last_sep));
    }
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    size_t written = fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return written == content.size();
}

std::string read_text_file(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return ""; }
    std::string s;
    s.resize(sz);
    fread(&s[0], 1, sz, f);
    fclose(f);
    return s;
}

std::string resolve_asset(const char* rel_path, const char* env_override) {
    if (!rel_path) return "";

    // 1. Specific or generic environment variable override
    if (env_override) {
        const char* val = getenv(env_override);
        if (val && *val && file_exists(val)) return std::string(val);
    }
    std::string env_assets_key = s_app_name + "_ASSETS_DIR";
    for (char& c : env_assets_key) c = toupper(c);
    const char* assets_env = getenv(env_assets_key.c_str());
    if (!assets_env) assets_env = getenv("CUBEFORGE_ASSETS_DIR");
    if (assets_env && *assets_env) {
        std::string cand = std::string(assets_env) + "/" + rel_path;
        if (file_exists(cand)) return cand;
    }
    std::string env_data_key = s_app_name + "_DATA_DIR";
    for (char& c : env_data_key) c = toupper(c);
    const char* data_env = getenv(env_data_key.c_str());
    if (!data_env) data_env = getenv("CUBEFORGE_DATA_DIR");
    if (data_env && *data_env) {
        std::string cand = std::string(data_env) + "/assets/" + rel_path;
        if (file_exists(cand)) return cand;
    }

    std::string exe_dir = get_exe_dir();

    // 2. Portable executable directory (<exe_dir>/assets/...)
    {
        std::string cand = exe_dir + "/assets/" + rel_path;
        if (file_exists(cand)) return cand;
    }

    // 3. Current working directory (<cwd>/assets/..., <cwd>/editor/assets/...)
    {
        std::string cand = std::string("assets/") + rel_path;
        if (file_exists(cand)) return cand;
        cand = std::string("templates/raylib/editor/assets/") + rel_path;
        if (file_exists(cand)) return cand;
        cand = std::string("editor/assets/") + rel_path;
        if (file_exists(cand)) return cand;
    }

    // 4. Linux FHS install path (<exe_dir>/../share/cubeforge/assets/...)
    {
        std::string cand = exe_dir + "/../share/" + s_app_name + "/assets/" + rel_path;
        if (file_exists(cand)) return cand;
    }

    // 5. User data directory (~/.local/share/cubeforge/assets/... or %LOCALAPPDATA%\cubeforge\assets\...)
    {
        std::string user_data = get_user_data_dir(s_app_name.c_str());
        if (!user_data.empty()) {
            std::string cand = user_data + "/assets/" + rel_path;
            if (file_exists(cand)) return cand;
        }
    }

    // 6. System install directory (/usr/share/cubeforge/assets/...)
    {
        std::string cand = std::string("/usr/share/") + s_app_name + "/assets/" + rel_path;
        if (file_exists(cand)) return cand;
        cand = std::string("/usr/local/share/") + s_app_name + "/assets/" + rel_path;
    }

    // Fallback: return default relative path
    return std::string("assets/") + rel_path;
}

std::string resolve_lua_dir(const char* root_override) {
    std::string env_lua_key = s_app_name + "_LUA_DIR";
    for (char& c : env_lua_key) c = toupper(c);
    const char* env_dir = getenv(env_lua_key.c_str());
    if (!env_dir) env_dir = getenv("CUBEFORGE_LUA_DIR");
    if (env_dir && *env_dir && dir_exists(env_dir)) {
        return std::string(env_dir);
    }

    if (root_override && *root_override) {
        std::string cand = std::string(root_override) + "/editor/lua";
        if (dir_exists(cand)) return cand;
        cand = std::string(root_override) + "/lua";
        if (dir_exists(cand)) return cand;
        if (file_exists(std::string(root_override) + "/main.lua")) {
            return std::string(root_override);
        }
    }

    std::string exe_dir = get_exe_dir();

    // 1. Portable: <exe_dir>/lua or <exe_dir>/editor/lua
    {
        std::string cand = exe_dir + "/lua";
        if (dir_exists(cand)) return cand;
        cand = exe_dir + "/editor/lua";
        if (dir_exists(cand)) return cand;
    }

    // 2. Working directory: editor/lua, lua, templates/raylib/editor/lua
    {
        if (dir_exists("editor/lua")) return "editor/lua";
        if (dir_exists("lua")) return "lua";
        if (dir_exists("templates/raylib/editor/lua")) return "templates/raylib/editor/lua";
    }

    // 3. Linux FHS install path: <exe_dir>/../share/cubeforge/lua
    {
        std::string cand = exe_dir + "/../share/" + s_app_name + "/lua";
        if (dir_exists(cand)) return cand;
    }

    // 4. System share path
    {
        if (dir_exists("/usr/share/" + s_app_name + "/lua")) return "/usr/share/" + s_app_name + "/lua";
        if (dir_exists("/usr/local/share/" + s_app_name + "/lua")) return "/usr/local/share/" + s_app_name + "/lua";
    }

    return "lua";
}

std::string get_user_config_dir(const char* app_name) {
    std::string base;
#ifdef _WIN32
    const char* appdata = getenv("APPDATA");
    if (appdata && *appdata) {
        base = appdata;
    } else {
        const char* userprofile = getenv("USERPROFILE");
        base = userprofile ? (std::string(userprofile) + "\\AppData\\Roaming") : ".";
    }
    std::string full = base + "\\" + (app_name ? app_name : s_app_name);
#else
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        base = xdg_config;
    } else {
        const char* home = getenv("HOME");
        base = home ? (std::string(home) + "/.config") : ".";
    }
    std::string full = base + "/" + (app_name ? app_name : s_app_name);
#endif
    ensure_dir(full);
    return full;
}

std::string get_user_data_dir(const char* app_name) {
    std::string base;
#ifdef _WIN32
    const char* localappdata = getenv("LOCALAPPDATA");
    if (localappdata && *localappdata) {
        base = localappdata;
    } else {
        const char* appdata = getenv("APPDATA");
        base = appdata ? appdata : ".";
    }
    std::string full = base + "\\" + (app_name ? app_name : s_app_name);
#else
    const char* xdg_data = getenv("XDG_DATA_HOME");
    if (xdg_data && *xdg_data) {
        base = xdg_data;
    } else {
        const char* home = getenv("HOME");
        base = home ? (std::string(home) + "/.local/share") : ".";
    }
    std::string full = base + "/" + (app_name ? app_name : s_app_name);
#endif
    ensure_dir(full);
    return full;
}

std::string get_user_documents_dir(const char* app_name) {
    std::string base;
#ifdef _WIN32
    const char* userprofile = getenv("USERPROFILE");
    base = userprofile ? (std::string(userprofile) + "\\Documents") : ".";
    std::string full = base + "\\" + (app_name ? app_name : s_app_title);
#else
    const char* xdg_docs = getenv("XDG_DOCUMENTS_DIR");
    if (xdg_docs && *xdg_docs) {
        base = xdg_docs;
    } else {
        const char* home = getenv("HOME");
        base = home ? (std::string(home) + "/Documents") : ".";
    }
    std::string full = base + "/" + (app_name ? app_name : s_app_title);
#endif
    ensure_dir(full);
    return full;
}

std::string get_user_projects_dir(const char* app_name) {
    std::string docs = get_user_documents_dir(app_name);
#ifdef _WIN32
    std::string full = docs + "\\Projects";
#else
    std::string full = docs + "/Projects";
#endif
    ensure_dir(full);
    return full;
}

// ── Lua Bindings ─────────────────────────────────────────────────────────────

static int l_app_get_app_name(lua_State* L) {
    lua_pushstring(L, s_app_name.c_str());
    return 1;
}

static int l_app_get_app_title(lua_State* L) {
    lua_pushstring(L, s_app_title.c_str());
    return 1;
}

static int l_app_set_app_name(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* title = lua_isnoneornil(L, 2) ? nullptr : luaL_checkstring(L, 2);
    set_app_name(name, title);
    return 0;
}

static int l_app_get_config_dir(lua_State* L) {
    const char* app = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
    std::string dir = get_user_config_dir(app);
    lua_pushstring(L, dir.c_str());
    return 1;
}

static int l_app_get_data_dir(lua_State* L) {
    const char* app = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
    std::string dir = get_user_data_dir(app);
    lua_pushstring(L, dir.c_str());
    return 1;
}

static int l_app_get_documents_dir(lua_State* L) {
    const char* app = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
    std::string dir = get_user_documents_dir(app);
    lua_pushstring(L, dir.c_str());
    return 1;
}

static int l_app_get_projects_dir(lua_State* L) {
    const char* app = lua_isnoneornil(L, 1) ? nullptr : luaL_checkstring(L, 1);
    std::string dir = get_user_projects_dir(app);
    lua_pushstring(L, dir.c_str());
    return 1;
}
static int l_app_ensure_dir(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    bool ok = ensure_dir(path);
    lua_pushboolean(L, ok);
    return 1;
}

static int l_app_save_user_file(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    const char* content = luaL_checkstring(L, 2);
    const char* sub = lua_isnoneornil(L, 3) ? "config" : luaL_checkstring(L, 3);

    std::string base = (strcmp(sub, "data") == 0) ? get_user_data_dir(s_app_name.c_str()) : get_user_config_dir(s_app_name.c_str());
#ifdef _WIN32
    std::string full = base + "\\" + filename;
#else
    std::string full = base + "/" + filename;
#endif
    bool ok = write_text_file(full, content);
    lua_pushboolean(L, ok);
    lua_pushstring(L, full.c_str());
    return 2;
}

static int l_app_load_user_file(lua_State* L) {
    const char* filename = luaL_checkstring(L, 1);
    const char* sub = lua_isnoneornil(L, 2) ? "config" : luaL_checkstring(L, 2);

    std::string base = (strcmp(sub, "data") == 0) ? get_user_data_dir(s_app_name.c_str()) : get_user_config_dir(s_app_name.c_str());
#ifdef _WIN32
    std::string full = base + "\\" + filename;
#else
    std::string full = base + "/" + filename;
#endif
    if (!file_exists(full)) {
        lua_pushnil(L);
        return 1;
    }
    std::string content = read_text_file(full);
    lua_pushstring(L, content.c_str());
    return 1;
}

static int l_app_resolve_asset(lua_State* L) {
    const char* rel_path = luaL_checkstring(L, 1);
    const char* env_var = lua_isnoneornil(L, 2) ? nullptr : luaL_checkstring(L, 2);
    std::string resolved = resolve_asset(rel_path, env_var);
    lua_pushstring(L, resolved.c_str());
    return 1;
}

void register_lua_bindings(lua_State* L, const char* backend_name) {
    lua_getglobal(L, "lp");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "lp");
        lua_getglobal(L, "lp");
    }

    lua_getfield(L, -1, "app");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
    }
    lua_pushcfunction(L, l_app_get_app_name);
    lua_setfield(L, -2, "get_app_name");

    lua_pushcfunction(L, l_app_get_app_title);
    lua_setfield(L, -2, "get_app_title");

    lua_pushcfunction(L, l_app_set_app_name);
    lua_setfield(L, -2, "set_app_name");

    lua_pushcfunction(L, l_app_get_config_dir);
    lua_setfield(L, -2, "get_config_dir");

    lua_pushcfunction(L, l_app_get_data_dir);
    lua_setfield(L, -2, "get_data_dir");

    lua_pushcfunction(L, l_app_get_documents_dir);
    lua_setfield(L, -2, "get_documents_dir");

    lua_pushcfunction(L, l_app_get_projects_dir);
    lua_setfield(L, -2, "get_projects_dir");
    lua_pushcfunction(L, [](lua_State* L) -> int {
        const char* p = luaL_checkstring(L, 1);
        lua_pushboolean(L, file_exists(p) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "file_exists");

    lua_pushcfunction(L, [](lua_State* L) -> int {
        const char* p = luaL_checkstring(L, 1);
        lua_pushboolean(L, dir_exists(p) ? 1 : 0);
        return 1;
    });
    lua_setfield(L, -2, "dir_exists");

    lua_pushcfunction(L, [](lua_State* L) -> int {
        const char* title = luaL_optstring(L, 1, "Open File");
        const char* default_path = luaL_optstring(L, 2, "");
        const char* filter = luaL_optstring(L, 3, "*.*");
        const char* filters[] = { filter };
        const char* res = tinyfd_openFileDialog(title, default_path, 1, filters, nullptr, 0);
        if (res) {
            lua_pushstring(L, res);
        } else {
            lua_pushnil(L);
        }
        return 1;
    });
    lua_setfield(L, -2, "open_file_dialog");

    lua_pushcfunction(L, l_app_ensure_dir);
    lua_setfield(L, -2, "ensure_dir");

    lua_pushcfunction(L, l_app_save_user_file);
    lua_setfield(L, -2, "save_user_file");

    lua_pushcfunction(L, l_app_load_user_file);
    lua_setfield(L, -2, "load_user_file");

    lua_pushcfunction(L, l_app_resolve_asset);
    lua_setfield(L, -2, "resolve_asset");
    static std::string s_backend_name;
    s_backend_name = backend_name ? backend_name : "Unknown";
    lua_pushstring(L, s_backend_name.c_str());
    lua_setfield(L, -2, "backend_name");
    lua_pushcfunction(L, [](lua_State* L) -> int {
        lua_pushstring(L, s_backend_name.c_str());
        return 1;
    });
    lua_setfield(L, -2, "get_backend_name");

    lua_setfield(L, -2, "app");
    lua_pop(L, 1); // pop lp
}

} // namespace app_paths
