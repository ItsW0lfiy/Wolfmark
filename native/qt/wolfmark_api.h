#pragma once

#include <cstddef>
#include <cstdint>

struct WolfmarkBuffer {
    std::uint8_t* data;
    std::size_t len;
    std::size_t capacity;
};

struct WolfmarkImageResult {
    std::uint32_t id;
    std::uint32_t width;
    std::uint32_t height;
    WolfmarkBuffer pixels;
    WolfmarkBuffer error;
};

struct WolfmarkCounters {
    std::uint64_t parse_count;
    std::uint64_t load_count;
    std::uint64_t image_request_count;
    std::uint64_t image_cache_bytes;
};

struct WolfmarkApiTable {
    std::uint32_t version;
    void* (*backend_new)();
    void (*backend_free)(void*);
    WolfmarkBuffer (*open_document)(void*, const std::uint8_t*, std::size_t);
    bool (*queue_image)(void*, std::uint32_t, std::uint32_t);
    WolfmarkImageResult (*poll_image)(const void*);
    WolfmarkCounters (*backend_counters)(const void*);
    WolfmarkBuffer (*select_update)(const std::uint8_t*, std::size_t, bool);
    WolfmarkBuffer (*verify_update)(const std::uint8_t*, std::size_t,
                                    const std::uint8_t*, std::size_t,
                                    const std::uint8_t*, std::size_t);
    void (*buffer_free)(WolfmarkBuffer);
    void* (*window_state_new)();
    void (*window_state_free)(void*);
    void (*window_set_mode)(void*, std::uint8_t);
    std::uint8_t (*window_enter_fullscreen)(void*);
    std::uint8_t (*window_leave_fullscreen)(void*);
};

static_assert(sizeof(WolfmarkBuffer) == sizeof(void*) + sizeof(std::size_t) * 2);
