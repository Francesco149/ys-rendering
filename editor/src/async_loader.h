// async_loader.h — Asynchronous background task queue & asset streaming engine
#pragma once

#include <string>
#include <vector>
#include <deque>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <raylib.h>

struct lua_State;

namespace async_io {

enum class TaskType {
    READ_ARCHIVE_FILE,
    DECODE_DDS_IMAGE,
    LOAD_ARCHIVE_TEXTURE,
};

struct AsyncTask {
    uint64_t id;
    std::string tag;
    TaskType type;
    std::atomic<bool> canceled{ false };

    // Input parameters
    std::string archive_path;
    std::string file_path;
    std::vector<std::string> candidate_paths;
    std::vector<uint8_t> input_bytes;
    bool auto_lum_alpha{ false };
    // Output results (CPU-side only, ready for main thread GPU upload)
    bool success{ false };
    std::string error_msg;
    std::string resolved_path;
    std::vector<uint8_t> result_bytes;
    Image result_image;
    bool has_image{ false };
    int width{ 0 };
    int height{ 0 };

    AsyncTask() : id(0), type(TaskType::READ_ARCHIVE_FILE), result_image{} {}
    ~AsyncTask() {
        if (has_image && result_image.data) {
            UnloadImage(result_image);
            result_image.data = nullptr;
        }
    }
};

class AsyncQueue {
public:
    static AsyncQueue& instance();

    uint64_t submit_read_archive(const std::string& arch_path, const std::string& file_path, const std::string& tag = "");
    uint64_t submit_decode_dds(const uint8_t* data, size_t size, bool auto_lum_alpha = false, const std::string& tag = "");
    uint64_t submit_load_archive_texture(const std::string& arch_path, const std::vector<std::string>& candidate_paths, bool auto_lum_alpha = false, const std::string& tag = "");
    void cancel_task(uint64_t task_id);
    void cancel_tag(const std::string& tag);
    void clear_pending();

    // Polls completed tasks from main thread (non-blocking)
    std::vector<std::shared_ptr<AsyncTask>> poll_completed(size_t max_results = 32);

    void shutdown();

private:
    AsyncQueue();
    ~AsyncQueue();

    void worker_thread_loop();

    std::atomic<bool> m_running{ true };
    std::atomic<uint64_t> m_next_id{ 1 };

    std::vector<std::thread> m_workers;
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::deque<std::shared_ptr<AsyncTask>> m_pending_queue;

    std::mutex m_completed_mutex;
    std::vector<std::shared_ptr<AsyncTask>> m_completed_queue;

    std::mutex m_tasks_mutex;
    std::unordered_map<uint64_t, std::shared_ptr<AsyncTask>> m_active_tasks;
    std::unordered_map<std::string, std::vector<std::shared_ptr<AsyncTask>>> m_tag_to_tasks;
};

void register_async_lua(lua_State* L);

} // namespace async_io
