#pragma once

#include <cstddef>
#include <cstdint>

struct MoonmarkBuffer {
    std::uint8_t* data;
    std::size_t len;
    std::size_t capacity;
};

struct MoonmarkImageResult {
    std::uint32_t id;
    std::uint32_t width;
    std::uint32_t height;
    MoonmarkBuffer pixels;
    MoonmarkBuffer error;
};

struct MoonmarkCounters {
    std::uint64_t parse_count;
    std::uint64_t load_count;
    std::uint64_t image_request_count;
    std::uint64_t image_cache_bytes;
};

struct MoonmarkApiTable {
    std::uint32_t version;
    void* (*backend_new)();
    void (*backend_free)(void*);
    MoonmarkBuffer (*open_document)(void*, const std::uint8_t*, std::size_t);
    bool (*queue_image)(void*, std::uint32_t, std::uint32_t);
    MoonmarkImageResult (*poll_image)(const void*);
    MoonmarkCounters (*backend_counters)(const void*);
    MoonmarkBuffer (*select_update)(const std::uint8_t*, std::size_t, bool);
    MoonmarkBuffer (*verify_update)(const std::uint8_t*, std::size_t,
                                    const std::uint8_t*, std::size_t,
                                    const std::uint8_t*, std::size_t);
    void (*buffer_free)(MoonmarkBuffer);
    void* (*window_state_new)();
    void (*window_state_free)(void*);
    void (*window_set_mode)(void*, std::uint8_t);
    std::uint8_t (*window_enter_fullscreen)(void*);
    std::uint8_t (*window_leave_fullscreen)(void*);
};

static_assert(sizeof(MoonmarkBuffer) == sizeof(void*) + sizeof(std::size_t) * 2);
