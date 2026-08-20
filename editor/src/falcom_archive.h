// falcom_archive.h — High-performance Falcom .na/.ni archive reader
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

struct lua_State;

namespace falcom {

struct ArchiveEntry {
    uint32_t id;
    uint32_t compressed_size;
    uint32_t data_offset;
    uint32_t name_offset;
    std::string name;          // Decrypted original name (e.g. "MAP\S_01\S_0100\S_0100.SOB")
    std::string normalized_name; // Lowercase with '/' separators (e.g. "map/s_01/s_0100/s_0100.sob")
    bool is_compressed;        // True if ends in .z or compressed payload
};

class Archive {
public:
    Archive();
    ~Archive();

    bool open(const std::string& ni_path, const std::string& na_path = "");
    void close();
    bool is_open() const { return m_open; }

    const std::string& get_ni_path() const { return m_ni_path; }
    const std::string& get_na_path() const { return m_na_path; }

    bool has_file(const std::string& path) const;
    const ArchiveEntry* find_entry(const std::string& path) const;
    const std::vector<ArchiveEntry>& get_entries() const { return m_entries; }

    // Read and decompress file contents. Returns empty vector on failure.
    std::vector<uint8_t> read_file(const std::string& path);
    std::vector<uint8_t> read_entry(const ArchiveEntry& entry);

    static std::string normalize_path(const std::string& path);

private:
    bool m_open;
    std::string m_ni_path;
    std::string m_na_path;
    std::vector<ArchiveEntry> m_entries;
    std::unordered_map<std::string, size_t> m_lookup; // normalized_path -> index in m_entries
    FILE* m_na_file;
};

// Archive Manager / Global Registry for Lua
class ArchiveManager {
public:
    static ArchiveManager& instance();

    int open_archive(const std::string& ni_path, const std::string& na_path = "");
    void close_archive(int handle);
    Archive* get_archive(int handle);
    void close_all();

private:
    ArchiveManager() : m_next_handle(1) {}
    int m_next_handle;
    std::unordered_map<int, std::unique_ptr<Archive>> m_archives;
};

void register_archive_lua(lua_State* L);

} // namespace falcom
