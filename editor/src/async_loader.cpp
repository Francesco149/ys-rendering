// async_loader.cpp — Background task queue & asset streaming engine implementation
#include "async_loader.h"
#include "falcom_archive.h"
#include "dds_loader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

static thread_local std::unordered_map<std::string, std::unique_ptr<falcom::Archive>> s_thread_archives;

static falcom::Archive* get_thread_archive(const std::string& arch_path) {
    if (arch_path.empty()) return nullptr;
    auto it = s_thread_archives.find(arch_path);
    if (it != s_thread_archives.end()) {
        return it->second.get();
    }
    auto arch = std::make_unique<falcom::Archive>();
    if (arch->open(arch_path)) {
        falcom::Archive* ptr = arch.get();
        s_thread_archives[arch_path] = std::move(arch);
        return ptr;
    }
    return nullptr;
}

namespace async_io {

AsyncQueue& AsyncQueue::instance() {
    static AsyncQueue s_inst;
    return s_inst;
}

AsyncQueue::AsyncQueue() {
    unsigned int num_workers = std::max(2u, std::min(4u, std::thread::hardware_concurrency()));
    m_workers.reserve(num_workers);
    for (unsigned int i = 0; i < num_workers; i++) {
        m_workers.emplace_back(&AsyncQueue::worker_thread_loop, this);
    }
}

AsyncQueue::~AsyncQueue() {
    shutdown();
}

void AsyncQueue::shutdown() {
    m_running = false;
    m_cv.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workers.clear();
}

uint64_t AsyncQueue::submit_read_archive(const std::string& arch_path, const std::string& file_path, const std::string& tag) {
    auto task = std::make_shared<AsyncTask>();
    task->id = m_next_id++;
    task->tag = tag;
    task->type = TaskType::READ_ARCHIVE_FILE;
    task->archive_path = arch_path;
    task->file_path = file_path;

    {
        std::lock_guard<std::mutex> lock(m_tasks_mutex);
        m_active_tasks[task->id] = task;
        if (!tag.empty()) {
            m_tag_to_tasks[tag].push_back(task);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_pending_queue.push_back(task);
    }
    m_cv.notify_one();
    return task->id;
}

uint64_t AsyncQueue::submit_decode_dds(const uint8_t* data, size_t size, bool auto_lum_alpha, const std::string& tag) {
    if (!data || size == 0) return 0;

    auto task = std::make_shared<AsyncTask>();
    task->id = m_next_id++;
    task->tag = tag;
    task->type = TaskType::DECODE_DDS_IMAGE;
    task->input_bytes.assign(data, data + size);
    task->auto_lum_alpha = auto_lum_alpha;

    {
        std::lock_guard<std::mutex> lock(m_tasks_mutex);
        m_active_tasks[task->id] = task;
        if (!tag.empty()) {
            m_tag_to_tasks[tag].push_back(task);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_pending_queue.push_back(task);
    }
    m_cv.notify_one();
    return task->id;
}
uint64_t AsyncQueue::submit_load_archive_texture(const std::string& arch_path, const std::vector<std::string>& candidate_paths, bool auto_lum_alpha, const std::string& tag) {
    if (arch_path.empty() || candidate_paths.empty()) return 0;

    auto task = std::make_shared<AsyncTask>();
    task->id = m_next_id++;
    task->tag = tag;
    task->type = TaskType::LOAD_ARCHIVE_TEXTURE;
    task->archive_path = arch_path;
    task->candidate_paths = candidate_paths;
    task->auto_lum_alpha = auto_lum_alpha;

    {
        std::lock_guard<std::mutex> lock(m_tasks_mutex);
        m_active_tasks[task->id] = task;
        if (!tag.empty()) {
            m_tag_to_tasks[tag].push_back(task);
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_pending_queue.push_back(task);
    }
    m_cv.notify_one();
    return task->id;
}


void AsyncQueue::cancel_task(uint64_t task_id) {
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    auto it = m_active_tasks.find(task_id);
    if (it != m_active_tasks.end()) {
        it->second->canceled = true;
    }
}

void AsyncQueue::cancel_tag(const std::string& tag) {
    if (tag.empty()) return;
    std::lock_guard<std::mutex> lock(m_tasks_mutex);
    auto it = m_tag_to_tasks.find(tag);
    if (it != m_tag_to_tasks.end()) {
        for (auto& task : it->second) {
            task->canceled = true;
        }
        m_tag_to_tasks.erase(it);
    }
}

void AsyncQueue::clear_pending() {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    for (auto& task : m_pending_queue) {
        task->canceled = true;
    }
    m_pending_queue.clear();
}

void AsyncQueue::worker_thread_loop() {
    while (m_running) {
        std::shared_ptr<AsyncTask> task;
        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            m_cv.wait(lock, [this] {
                return !m_running || !m_pending_queue.empty();
            });

            if (!m_running) break;

            if (!m_pending_queue.empty()) {
                task = m_pending_queue.front();
                m_pending_queue.pop_front();
            }
        }

        if (!task || task->canceled) {
            continue;
        }

        // Process Task
        if (task->type == TaskType::READ_ARCHIVE_FILE) {
            falcom::Archive* arch = get_thread_archive(task->archive_path);
            if (arch && arch->is_open()) {
                if (!task->canceled) {
                    task->result_bytes = arch->read_file(task->file_path);
                    task->success = !task->result_bytes.empty();
                    if (!task->success) {
                        task->error_msg = "File not found in archive: " + task->file_path;
                    }
                }
            } else {
                task->success = false;
                task->error_msg = "Failed to open archive: " + task->archive_path;
            }
        } else if (task->type == TaskType::LOAD_ARCHIVE_TEXTURE) {
            falcom::Archive* arch = get_thread_archive(task->archive_path);
            if (arch && arch->is_open()) {
                const falcom::ArchiveEntry* found_entry = nullptr;
                for (const auto& cand : task->candidate_paths) {
                    if (task->canceled) break;
                    const falcom::ArchiveEntry* entry = arch->find_entry(cand);
                    if (entry) {
                        found_entry = entry;
                        task->resolved_path = falcom::Archive::normalize_path(cand);
                        break;
                    }
                }
                if (found_entry && !task->canceled) {
                    std::vector<uint8_t> bytes = arch->read_entry(*found_entry);
                    if (!bytes.empty() && !task->canceled) {
                        Image img = falcom::DdsLoader::load_image_from_memory(bytes.data(), bytes.size(), task->auto_lum_alpha);
                        if (img.data && !task->canceled) {
                            task->result_image = img;
                            task->has_image = true;
                            task->width = img.width;
                            task->height = img.height;
                            task->success = true;
                        } else {
                            if (img.data) UnloadImage(img);
                            task->success = false;
                            task->error_msg = "Failed to decode DDS image";
                        }
                    } else {
                        task->success = false;
                        task->error_msg = "Failed to read entry from archive";
                    }
                } else if (!task->success) {
                    task->success = false;
                    task->error_msg = "No matching candidate texture in archive";
                }
            } else {
                task->success = false;
                task->error_msg = "Failed to open archive: " + task->archive_path;
            }
        } else if (task->type == TaskType::DECODE_DDS_IMAGE) {
            if (!task->canceled && !task->input_bytes.empty()) {
                Image img = falcom::DdsLoader::load_image_from_memory(task->input_bytes.data(), task->input_bytes.size(), task->auto_lum_alpha);
                if (img.data && !task->canceled) {
                    task->result_image = img;
                    task->has_image = true;
                    task->width = img.width;
                    task->height = img.height;
                    task->success = true;
                } else {
                    if (img.data) UnloadImage(img);
                    task->success = false;
                    task->error_msg = "Failed to decode DDS image";
                }
            }
        }

        if (task->canceled) {
            if (task->has_image && task->result_image.data) {
                UnloadImage(task->result_image);
                task->result_image.data = nullptr;
                task->has_image = false;
            }
            continue;
        }

        // Push to completed queue
        {
            std::lock_guard<std::mutex> lock(m_completed_mutex);
            m_completed_queue.push_back(task);
        }
    }
}

std::vector<std::shared_ptr<AsyncTask>> AsyncQueue::poll_completed(size_t max_results) {
    std::vector<std::shared_ptr<AsyncTask>> results;
    {
        std::lock_guard<std::mutex> lock(m_completed_mutex);
        size_t count = std::min(max_results, m_completed_queue.size());
        if (count > 0) {
            results.assign(m_completed_queue.begin(), m_completed_queue.begin() + count);
            m_completed_queue.erase(m_completed_queue.begin(), m_completed_queue.begin() + count);
        }
    }

    // Clean up from active tracking
    if (!results.empty()) {
        std::lock_guard<std::mutex> lock(m_tasks_mutex);
        for (const auto& task : results) {
            m_active_tasks.erase(task->id);
        }
    }

    return results;
}

// ── Lua Bindings ────────────────────────────────────────────────────────────

static int l_async_read_archive(lua_State* L) {
    const char* arch_p = luaL_checkstring(L, 1);
    const char* file_p = luaL_checkstring(L, 2);
    const char* tag = luaL_optstring(L, 3, "");

    uint64_t id = AsyncQueue::instance().submit_read_archive(arch_p, file_p, tag);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}

static int l_async_decode_dds(lua_State* L) {
    size_t len = 0;
    const char* bytes = luaL_checklstring(L, 1, &len);
    bool auto_lum = lua_toboolean(L, 2) != 0;
    const char* tag = luaL_optstring(L, 3, "");

    uint64_t id = AsyncQueue::instance().submit_decode_dds((const uint8_t*)bytes, len, auto_lum, tag);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}
static int l_async_load_archive_texture(lua_State* L) {
    const char* arch_p = luaL_checkstring(L, 1);
    if (!lua_istable(L, 2)) {
        return luaL_error(L, "Expected table of candidate paths as argument 2");
    }
    bool auto_lum = lua_toboolean(L, 3) != 0;
    const char* tag = luaL_optstring(L, 4, "");

    std::vector<std::string> candidates;
    int len = (int)lua_rawlen(L, 2);
    candidates.reserve(len);
    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, 2, i);
        if (lua_isstring(L, -1)) {
            candidates.emplace_back(lua_tostring(L, -1));
        }
        lua_pop(L, 1);
    }

    uint64_t id = AsyncQueue::instance().submit_load_archive_texture(arch_p, candidates, auto_lum, tag);
    lua_pushinteger(L, (lua_Integer)id);
    return 1;
}


static int l_async_cancel(lua_State* L) {
    uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    AsyncQueue::instance().cancel_task(id);
    return 0;
}

static int l_async_cancel_tag(lua_State* L) {
    const char* tag = luaL_checkstring(L, 1);
    AsyncQueue::instance().cancel_tag(tag);
    return 0;
}

static int l_async_clear_pending(lua_State* L) {
    AsyncQueue::instance().clear_pending();
    return 0;
}

static int l_async_poll_completed(lua_State* L) {
    size_t max_count = (size_t)luaL_optinteger(L, 1, 32);
    auto tasks = AsyncQueue::instance().poll_completed(max_count);

    lua_newtable(L);
    int idx = 1;

    for (auto& t : tasks) {
        lua_newtable(L);
        lua_pushinteger(L, (lua_Integer)t->id); lua_setfield(L, -2, "id");
        lua_pushstring(L, t->tag.c_str()); lua_setfield(L, -2, "tag");
        lua_pushboolean(L, t->success ? 1 : 0); lua_setfield(L, -2, "success");

        if (t->type == TaskType::READ_ARCHIVE_FILE) {
            lua_pushstring(L, "read_archive"); lua_setfield(L, -2, "type");
            if (t->success && !t->result_bytes.empty()) {
                lua_pushlstring(L, (const char*)t->result_bytes.data(), t->result_bytes.size());
                lua_setfield(L, -2, "data");
            }
        } else if (t->type == TaskType::DECODE_DDS_IMAGE) {
            lua_pushstring(L, "decode_dds"); lua_setfield(L, -2, "type");
            if (t->success && t->has_image && t->result_image.data) {
                // If OpenGL is ready, upload texture to GPU directly on main thread
                if (IsWindowReady()) {
                    Texture2D tex = LoadTextureFromImage(t->result_image);
                    UnloadImage(t->result_image);
                    t->result_image.data = nullptr;
                    t->has_image = false;
                    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
                    lua_pushinteger(L, tex.id); lua_setfield(L, -2, "tex_id");
                    lua_pushinteger(L, tex.width); lua_setfield(L, -2, "width");
                    lua_pushinteger(L, tex.height); lua_setfield(L, -2, "height");
                } else {
                    lua_pushinteger(L, 1); lua_setfield(L, -2, "tex_id");
                    lua_pushinteger(L, t->width); lua_setfield(L, -2, "width");
                    lua_pushinteger(L, t->height); lua_setfield(L, -2, "height");
                }
            }
        } else if (t->type == TaskType::LOAD_ARCHIVE_TEXTURE) {
            lua_pushstring(L, "load_texture"); lua_setfield(L, -2, "type");
            if (!t->resolved_path.empty()) {
                lua_pushstring(L, t->resolved_path.c_str()); lua_setfield(L, -2, "path");
            }
            if (t->success && t->has_image && t->result_image.data) {
                if (IsWindowReady()) {
                    Texture2D tex = LoadTextureFromImage(t->result_image);
                    UnloadImage(t->result_image);
                    t->result_image.data = nullptr;
                    t->has_image = false;
                    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
                    SetTextureWrap(tex, TEXTURE_WRAP_REPEAT);
                    lua_pushinteger(L, tex.id); lua_setfield(L, -2, "tex_id");
                    lua_pushinteger(L, tex.width); lua_setfield(L, -2, "width");
                    lua_pushinteger(L, tex.height); lua_setfield(L, -2, "height");
                } else {
                    lua_pushinteger(L, 1); lua_setfield(L, -2, "tex_id");
                    lua_pushinteger(L, t->width); lua_setfield(L, -2, "width");
                    lua_pushinteger(L, t->height); lua_setfield(L, -2, "height");
                }
            }
        }
        if (!t->success && !t->error_msg.empty()) {
            lua_pushstring(L, t->error_msg.c_str());
            lua_setfield(L, -2, "error");
        }

        lua_rawseti(L, -2, idx++);
    }

    return 1;
}

void register_async_lua(lua_State* L) {
    lua_getglobal(L, "lp");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, "lp");
        lua_getglobal(L, "lp");
    }

    lua_newtable(L);
    lua_pushcfunction(L, l_async_read_archive); lua_setfield(L, -2, "read_archive_file");
    lua_pushcfunction(L, l_async_decode_dds); lua_setfield(L, -2, "decode_dds");
    lua_pushcfunction(L, l_async_load_archive_texture); lua_setfield(L, -2, "load_archive_texture");
    lua_pushcfunction(L, l_async_cancel); lua_setfield(L, -2, "cancel");
    lua_pushcfunction(L, l_async_cancel_tag); lua_setfield(L, -2, "cancel_tag");
    lua_pushcfunction(L, l_async_clear_pending); lua_setfield(L, -2, "clear_pending");
    lua_pushcfunction(L, l_async_poll_completed); lua_setfield(L, -2, "poll_completed");

    lua_setfield(L, -2, "async");
    lua_pop(L, 1); // pop lp
}

} // namespace async_io
