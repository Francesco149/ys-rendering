// falcom_archive.cpp — High-performance Falcom .na/.ni archive reader implementation
#include "falcom_archive.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <zlib.h>
#include <raylib.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
namespace falcom {

static void decrypt_ni_buffer(uint8_t* data, size_t len) {
    uint32_t num = 0x7C53F961;
    for (size_t i = 0; i < len; i++) {
        num = (num * 0x3D09) & 0xFFFFFFFF;
        uint8_t shift = (uint8_t)((num >> 16) & 0xFF);
        data[i] = (uint8_t)(data[i] - shift);
    }
}

std::string Archive::normalize_path(const std::string& path) {
    std::string norm = path;
    for (char& c : norm) {
        if (c == '\\') c = '/';
        else c = (char)tolower((unsigned char)c);
    }
    // Remove leading "./" or "/" or "data/"
    while (norm.rfind("./", 0) == 0) norm = norm.substr(2);
    while (norm.rfind("/", 0) == 0) norm = norm.substr(1);
    while (norm.rfind("data/", 0) == 0) norm = norm.substr(5);
    return norm;
}

Archive::Archive() : m_open(false), m_na_file(nullptr) {}

Archive::~Archive() {
    close();
}

void Archive::close() {
    if (m_na_file) {
        fclose(m_na_file);
        m_na_file = nullptr;
    }
    m_entries.clear();
    m_lookup.clear();
    m_open = false;
}

bool Archive::open(const std::string& ni_path, const std::string& na_path) {
    close();
    m_ni_path = ni_path;
    if (!na_path.empty()) {
        m_na_path = na_path;
    } else {
        // Replace extension with .na
        m_na_path = ni_path;
        size_t dot = m_na_path.rfind('.');
        if (dot != std::string::npos) {
            m_na_path = m_na_path.substr(0, dot) + ".na";
        } else {
            m_na_path += ".na";
        }
    }

    FILE* ni_f = fopen(m_ni_path.c_str(), "rb");
    if (!ni_f) {
        return false;
    }

    // Read 16-byte header
    uint8_t header[16];
    if (fread(header, 1, 16, ni_f) != 16) {
        fclose(ni_f);
        return false;
    }

    // Magic: "NNI\0" (0x00494E4E in LE)
    if (memcmp(header, "NNI\0", 4) != 0) {
        fclose(ni_f);
        return false;
    }

    uint32_t total_entries = *(uint32_t*)(header + 4);
    uint32_t str_table_size = *(uint32_t*)(header + 8);

    if (total_entries == 0 || str_table_size == 0) {
        fclose(ni_f);
        return false;
    }

    // Read encrypted entry records
    size_t info_size = (size_t)total_entries * 16;
    std::vector<uint8_t> info_buf(info_size);
    if (fread(info_buf.data(), 1, info_size, ni_f) != info_size) {
        fclose(ni_f);
        return false;
    }

    // Read encrypted string table
    std::vector<uint8_t> str_buf(str_table_size);
    if (fread(str_buf.data(), 1, str_table_size, ni_f) != str_table_size) {
        fclose(ni_f);
        return false;
    }
    fclose(ni_f);

    // Decrypt buffers
    decrypt_ni_buffer(info_buf.data(), info_size);
    decrypt_ni_buffer(str_buf.data(), str_table_size);

    // Open .na file for subsequent reads
    m_na_file = fopen(m_na_path.c_str(), "rb");
    if (!m_na_file) {
        return false;
    }

    // Parse entries
    m_entries.reserve(total_entries);
    for (uint32_t i = 0; i < total_entries; i++) {
        const uint8_t* p = info_buf.data() + i * 16;
        uint32_t file_id = *(const uint32_t*)(p + 0);
        uint32_t comp_size = *(const uint32_t*)(p + 4);
        uint32_t data_off = *(const uint32_t*)(p + 8);
        uint32_t name_off = *(const uint32_t*)(p + 12);

        std::string raw_name;
        if (name_off < str_table_size) {
            const char* name_ptr = (const char*)(str_buf.data() + name_off);
            // Ensure null termination within bounds
            size_t max_len = str_table_size - name_off;
            size_t actual_len = strnlen(name_ptr, max_len);
            raw_name.assign(name_ptr, actual_len);
        }

        ArchiveEntry entry;
        entry.id = file_id;
        entry.compressed_size = comp_size;
        entry.data_offset = data_off;
        entry.name_offset = name_off;
        entry.name = raw_name;
        entry.normalized_name = normalize_path(raw_name);
        
        // Check if ends in .z
        std::string raw_norm = entry.normalized_name;
        entry.is_compressed = (raw_norm.length() >= 2 && raw_norm.compare(raw_norm.length() - 2, 2, ".z") == 0);

        std::string clean = raw_norm;
        if (entry.is_compressed) {
            clean = raw_norm.substr(0, raw_norm.length() - 2);
        }
        entry.normalized_name = clean;

        m_lookup[clean] = m_entries.size();
        m_lookup[raw_norm] = m_entries.size();
        m_entries.push_back(entry);
    }

    m_open = true;
    return true;
}

bool Archive::has_file(const std::string& path) const {
    std::string norm = normalize_path(path);
    return m_lookup.find(norm) != m_lookup.end();
}

const ArchiveEntry* Archive::find_entry(const std::string& path) const {
    std::string norm = normalize_path(path);
    auto it = m_lookup.find(norm);
    if (it != m_lookup.end()) {
        return &m_entries[it->second];
    }
    return nullptr;
}

std::vector<uint8_t> Archive::read_entry(const ArchiveEntry& entry) {
    if (!m_open || !m_na_file) return {};

    if (fseek(m_na_file, entry.data_offset, SEEK_SET) != 0) {
        return {};
    }

    std::vector<uint8_t> raw_buf(entry.compressed_size);
    if (fread(raw_buf.data(), 1, entry.compressed_size, m_na_file) != entry.compressed_size) {
        return {};
    }

    // Check if compressed (.z payload)
    if (entry.is_compressed && raw_buf.size() >= 8) {
        uint32_t expected_crc = *(const uint32_t*)(raw_buf.data() + 0);
        uint32_t uncomp_size = *(const uint32_t*)(raw_buf.data() + 4);
        (void)expected_crc;

        const unsigned char* comp_data = raw_buf.data() + 8;
        uLong comp_size = (uLong)(raw_buf.size() - 8);
        uLongf dest_len = (uLongf)uncomp_size;

        std::vector<uint8_t> out(uncomp_size);
        int res = uncompress((Bytef*)out.data(), &dest_len, (const Bytef*)comp_data, comp_size);
        if (res == Z_OK) {
            out.resize(dest_len);
            return out;
        } else {
            // Try raw inflate without zlib header if uncompress returned Z_DATA_ERROR
            z_stream strm = {};
            strm.next_in = (Bytef*)comp_data;
            strm.avail_in = comp_size;
            strm.next_out = (Bytef*)out.data();
            strm.avail_out = uncomp_size;

            if (inflateInit2(&strm, -MAX_WBITS) == Z_OK) {
                int inf_res = inflate(&strm, Z_FINISH);
                inflateEnd(&strm);
                if (inf_res == Z_STREAM_END || inf_res == Z_OK) {
                    out.resize(strm.total_out);
                    return out;
                }
            }
            return raw_buf;
        }
    }

    return raw_buf;
}

std::vector<uint8_t> Archive::read_file(const std::string& path) {
    const ArchiveEntry* entry = find_entry(path);
    if (!entry) return {};
    return read_entry(*entry);
}

// ── ArchiveManager ──────────────────────────────────────────────────────────

ArchiveManager& ArchiveManager::instance() {
    static ArchiveManager s_inst;
    return s_inst;
}

int ArchiveManager::open_archive(const std::string& ni_path, const std::string& na_path) {
    auto arch = std::make_unique<Archive>();
    if (!arch->open(ni_path, na_path)) {
        return -1;
    }
    int handle = m_next_handle++;
    m_archives[handle] = std::move(arch);
    return handle;
}

void ArchiveManager::close_archive(int handle) {
    m_archives.erase(handle);
}

Archive* ArchiveManager::get_archive(int handle) {
    auto it = m_archives.find(handle);
    if (it != m_archives.end()) return it->second.get();
    return nullptr;
}

void ArchiveManager::close_all() {
    m_archives.clear();
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_archive_open(lua_State* L) {
    const char* ni = luaL_checkstring(L, 1);
    const char* na = luaL_optstring(L, 2, "");
    int h = ArchiveManager::instance().open_archive(ni, na);
    if (h < 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "Failed to open archive: %s", ni);
        return 2;
    }
    lua_pushinteger(L, h);
    return 1;
}

static int l_archive_close(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    ArchiveManager::instance().close_archive(h);
    return 0;
}

static int l_archive_has_file(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* path = luaL_checkstring(L, 2);
    Archive* arch = ArchiveManager::instance().get_archive(h);
    if (!arch) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, arch->has_file(path) ? 1 : 0);
    return 1;
}

static int l_archive_read_file(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* path = luaL_checkstring(L, 2);
    Archive* arch = ArchiveManager::instance().get_archive(h);
    if (!arch) {
        lua_pushnil(L);
        lua_pushstring(L, "Invalid archive handle");
        return 2;
    }
    std::vector<uint8_t> data = arch->read_file(path);
    if (data.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L, "File not found or read failed: %s", path);
        return 2;
    }
    lua_pushlstring(L, (const char*)data.data(), data.size());
    return 1;
}

static int l_archive_list_files(lua_State* L) {
    int h = (int)luaL_checkinteger(L, 1);
    const char* pattern = luaL_optstring(L, 2, nullptr);
    Archive* arch = ArchiveManager::instance().get_archive(h);
    if (!arch) {
        lua_newtable(L);
        return 1;
    }

    std::string norm_pattern = pattern ? Archive::normalize_path(pattern) : "";

    const auto& entries = arch->get_entries();
    lua_newtable(L);
    int idx = 1;

    for (const auto& e : entries) {
        if (!norm_pattern.empty()) {
            if (e.normalized_name.find(norm_pattern) == std::string::npos) {
                continue;
            }
        }

        lua_newtable(L);
        lua_pushstring(L, e.name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, e.normalized_name.c_str());
        lua_setfield(L, -2, "path");
        lua_pushinteger(L, e.id);
        lua_setfield(L, -2, "id");
        lua_pushinteger(L, e.compressed_size);
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, e.is_compressed ? 1 : 0);
        lua_setfield(L, -2, "compressed");

        lua_rawseti(L, -2, idx++);
    }

    return 1;
}

void register_archive_lua(lua_State* L) {
    lua_getglobal(L, "ys");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "ys");
        lua_getglobal(L, "ys");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_archive_open);
    lua_setfield(L, -2, "open");
    lua_pushcfunction(L, l_archive_close);
    lua_setfield(L, -2, "close");
    lua_pushcfunction(L, l_archive_has_file);
    lua_setfield(L, -2, "has_file");
    lua_pushcfunction(L, l_archive_read_file);
    lua_setfield(L, -2, "read_file");
    lua_pushcfunction(L, l_archive_list_files);
    lua_setfield(L, -2, "list_files");

    lua_setfield(L, -2, "archive");
    lua_pop(L, 1); // pop "ys"
}

} // namespace falcom
