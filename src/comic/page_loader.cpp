#include "page_loader.hpp"

#include "cbz_book.hpp"
#include "image_display.hpp"

#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

extern "C" {
#include <epdiy.h>
}

// Two-worker preloader. SD extract is I/O-bound (most of its ~470 ms
// per page is CPU waiting on SDSPI DMA), and JPEG decode is purely
// CPU (~300 ms). Splitting them across two FreeRTOS tasks lets the
// decode task run while the extract task waits for the next SD
// transfer to complete; steady-state throughput becomes
// max(extract, decode) per page instead of their sum.
//
// See the ASCII pipeline diagram + ring-size discussion at the top of
// page_loader.hpp — kept there as the single source of truth so the
// numbers don't drift between the header and the implementation.
//
// Both workers share s_mutex and prioritise s_foreground_page (set
// by fetch_and_consume) over the in-window background pages.

namespace ps3::comic::page_loader {

namespace {
constexpr const char* TAG = "loader";

// JPEGDEC + miniz overflow 16 KB; both workers are independent so
// each gets its own budget.
constexpr uint32_t    LOADER_TASK_STACK    = 32768;
// Pin extract to Core 1 (next to touch_task) and decode to Core 0
// (next to main). The ESP-IDF SDSPI driver uses polling-mode SPI
// transactions, so extract busy-waits while reading the bus and
// won't yield CPU to a same-core decode worker. Splitting across
// cores is the only way to get real parallelism — same-core with
// priority tricks just round-robins them at the tick rate.
//
// Decode (prio 4) > main (prio 1), so during a page-flip burst it
// monopolises Core 0; main is mostly blocked on fetch_and_consume()
// at that point so this is fine in practice.
constexpr UBaseType_t EXTRACT_CORE         = 1;
constexpr UBaseType_t DECODE_CORE          = 0;
constexpr UBaseType_t EXTRACT_PRIORITY     = 4;
constexpr UBaseType_t DECODE_PRIORITY      = 4;

// Number of pre-extracted JPEG byte buffers held between the workers.
// 2 is enough to keep both workers fed in steady state (extract is
// the throughput bottleneck — extra slots would be wasted).
constexpr int BYTE_SLOTS = 2;

enum SlotStatus : uint8_t {
    SLOT_EMPTY    = 0,
    SLOT_DECODING = 1,
    SLOT_READY    = 2,
};

enum ByteStatus : uint8_t {
    BYTES_EMPTY   = 0,
    BYTES_FILLING = 1,
    BYTES_READY   = 2,
};

struct Slot {
    int        page;
    SlotStatus status;
    uint8_t*   fb;     // PSRAM, panel-sized 4 bpp packed
    int        slice_count;
};

struct ByteSlot {
    int        page;
    ByteStatus status;
    uint8_t*   data;   // PSRAM, allocated by extract via miniz, freed by decode
    size_t     size;
};

CbzBook*           s_book          = nullptr;
size_t             s_fb_size       = 0;
Slot               s_slots[DECODE_SLOTS] = {};
ByteSlot           s_bytes[BYTE_SLOTS]     = {};

TaskHandle_t       s_extract_task    = nullptr;
TaskHandle_t       s_decode_task     = nullptr;
SemaphoreHandle_t  s_mutex           = nullptr;
SemaphoreHandle_t  s_extract_work_sem = nullptr;
SemaphoreHandle_t  s_decode_work_sem  = nullptr;
SemaphoreHandle_t  s_done_sem         = nullptr;  // wakes fetch_and_consume()
SemaphoreHandle_t  s_extract_exit_sem = nullptr;
SemaphoreHandle_t  s_decode_exit_sem  = nullptr;
volatile bool      s_stop             = false;

int s_current_page    = -1;
int s_foreground_page = -1;
ImageFit s_fit = ImageFit::Page;
int s_slice_index = 0;
int s_current_slice_count = 1;

// --- Mutex-protected helpers ---

bool slot_holds(int page) {
    for (auto& s : s_slots) {
        if (s.status != SLOT_EMPTY && s.page == page) return true;
    }
    return false;
}

bool byte_holds(int page) {
    for (auto& b : s_bytes) {
        if (b.status != BYTES_EMPTY && b.page == page) return true;
    }
    return false;
}

// The preload window spans `[current - PRELOAD_BACKWARD, current +
// PRELOAD_FORWARD]` *including* the current page itself — try_consume
// keeps the current page slot READY so re-renders (toolbar redraws)
// and immediate back-taps stay instant. page < 0 is rejected for the
// early pages of a book.
bool page_in_window(int page) {
    if (page < 0) return false;
    return page >= s_current_page - PRELOAD_BACKWARD
        && page <= s_current_page + PRELOAD_FORWARD;
}

// Pick the next page the extract worker should fetch from CBZ.
// Order: foreground → forward (closest first) → backward (closest
// first). Forward dominates because users mostly read forward;
// backward fills in when the forward window is fully cached.
int pick_extract_target() {
    if (s_foreground_page >= 0
        && !byte_holds(s_foreground_page)
        && !slot_holds(s_foreground_page)) {
        return s_foreground_page;
    }
    if (s_current_page < 0) return -1;
    const int max_page = s_book ? s_book->page_count() - 1 : -1;
    if (max_page < 0) return -1;

    int upper = s_current_page + PRELOAD_FORWARD;
    if (upper > max_page) upper = max_page;
    for (int p = s_current_page + 1; p <= upper; ++p) {
        if (!byte_holds(p) && !slot_holds(p)) return p;
    }

    int lower = s_current_page - PRELOAD_BACKWARD;
    if (lower < 0) lower = 0;
    for (int p = s_current_page - 1; p >= lower; --p) {
        if (!byte_holds(p) && !slot_holds(p)) return p;
    }
    return -1;
}

// Reserve a byte slot for `target`. EMPTY first, then evict an
// out-of-window READY slot. Foreground priority can also evict an
// in-window READY slot. The current foreground page is never evicted
// (would race the decode worker that's about to consume it).
int reserve_byte_slot(int target, bool is_foreground) {
    for (int i = 0; i < BYTE_SLOTS; ++i) {
        if (s_bytes[i].status == BYTES_EMPTY) return i;
    }
    for (int i = 0; i < BYTE_SLOTS; ++i) {
        if (s_bytes[i].status == BYTES_READY
            && !page_in_window(s_bytes[i].page)
            && s_bytes[i].page != target
            && s_bytes[i].page != s_foreground_page) {
            return i;
        }
    }
    if (!is_foreground) return -1;
    int best = -1;
    int best_page = -1;
    for (int i = 0; i < BYTE_SLOTS; ++i) {
        if (s_bytes[i].status == BYTES_READY
            && s_bytes[i].page != target
            && s_bytes[i].page != s_foreground_page
            && s_bytes[i].page > best_page) {
            best = i;
            best_page = s_bytes[i].page;
        }
    }
    return best;
}

// Pick a byte slot whose JPEG bytes are ready and whose page still
// needs to be decoded into a slot. Same priority order as
// pick_extract_target: foreground → forward → backward.
int pick_decode_byte_slot() {
    if (s_foreground_page >= 0 && !slot_holds(s_foreground_page)) {
        for (int i = 0; i < BYTE_SLOTS; ++i) {
            if (s_bytes[i].status == BYTES_READY
                && s_bytes[i].page == s_foreground_page) {
                return i;
            }
        }
    }
    if (s_current_page < 0) return -1;
    const int max_page = s_book ? s_book->page_count() - 1 : -1;
    if (max_page < 0) return -1;

    int upper = s_current_page + PRELOAD_FORWARD;
    if (upper > max_page) upper = max_page;
    for (int p = s_current_page + 1; p <= upper; ++p) {
        if (slot_holds(p)) continue;
        for (int i = 0; i < BYTE_SLOTS; ++i) {
            if (s_bytes[i].status == BYTES_READY && s_bytes[i].page == p) {
                return i;
            }
        }
    }

    int lower = s_current_page - PRELOAD_BACKWARD;
    if (lower < 0) lower = 0;
    for (int p = s_current_page - 1; p >= lower; --p) {
        if (slot_holds(p)) continue;
        for (int i = 0; i < BYTE_SLOTS; ++i) {
            if (s_bytes[i].status == BYTES_READY && s_bytes[i].page == p) {
                return i;
            }
        }
    }
    return -1;
}

// Reserve a decode slot for `target`. EMPTY first, then out-of-window
// READY, then (foreground only) any READY. Foreground page and
// current_page are never evicted — current_page must stay cached so
// consume hits remain instant on re-renders and back-taps.
int reserve_decode_slot(int target, bool is_foreground) {
    for (int i = 0; i < DECODE_SLOTS; ++i) {
        if (s_slots[i].status == SLOT_EMPTY) return i;
    }
    for (int i = 0; i < DECODE_SLOTS; ++i) {
        if (s_slots[i].status == SLOT_READY
            && !page_in_window(s_slots[i].page)
            && s_slots[i].page != target
            && s_slots[i].page != s_foreground_page
            && s_slots[i].page != s_current_page) {
            return i;
        }
    }
    if (!is_foreground) return -1;
    int best = -1;
    int best_page = -1;
    for (int i = 0; i < DECODE_SLOTS; ++i) {
        if (s_slots[i].status == SLOT_READY
            && s_slots[i].page != target
            && s_slots[i].page != s_foreground_page
            && s_slots[i].page != s_current_page
            && s_slots[i].page > best_page) {
            best = i;
            best_page = s_slots[i].page;
        }
    }
    return best;
}

// Drop READY slots (both byte cache and decode cache) whose page no
// longer belongs to the preload window. Called when current_page
// changes via request().
void evict_outside_window() {
    for (auto& s : s_slots) {
        if (s.status == SLOT_READY
            && !page_in_window(s.page)
            && s.page != s_foreground_page) {
            s.status = SLOT_EMPTY;
        }
    }
    for (auto& b : s_bytes) {
        if (b.status == BYTES_READY
            && !page_in_window(b.page)
            && b.page != s_foreground_page) {
            if (b.data) std::free(b.data);
            b.data = nullptr;
            b.size = 0;
            b.status = BYTES_EMPTY;
            b.page = -1;
        }
    }
}

// --- Extract worker ---

void extract_task(void*) {
    while (!s_stop) {
        if (xSemaphoreTake(s_extract_work_sem, portMAX_DELAY) != pdTRUE) continue;
        if (s_stop) break;

        while (!s_stop) {
            int target = -1;
            int slot   = -1;

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            target = pick_extract_target();
            if (target >= 0) {
                const bool is_fg = (target == s_foreground_page);
                slot = reserve_byte_slot(target, is_fg);
                if (slot >= 0) {
                    if (s_bytes[slot].data) {
                        std::free(s_bytes[slot].data);
                        s_bytes[slot].data = nullptr;
                    }
                    s_bytes[slot].page   = target;
                    s_bytes[slot].size   = 0;
                    s_bytes[slot].status = BYTES_FILLING;
                } else {
                    target = -1;
                }
            }
            xSemaphoreGive(s_mutex);

            if (target < 0) break;

            const int64_t t0 = esp_timer_get_time();
            uint8_t* jpg = nullptr;
            size_t   jpg_size = 0;
            const bool ok = s_book
                && s_book->extract(target, &jpg, &jpg_size, nullptr, 0);
            const int64_t t1 = esp_timer_get_time();

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const bool still_useful = ok
                && (target == s_foreground_page || page_in_window(target));
            if (still_useful) {
                s_bytes[slot].data   = jpg;
                s_bytes[slot].size   = jpg_size;
                s_bytes[slot].status = BYTES_READY;
            } else {
                s_bytes[slot].data   = nullptr;
                s_bytes[slot].size   = 0;
                s_bytes[slot].page   = -1;
                s_bytes[slot].status = BYTES_EMPTY;
                if (jpg) std::free(jpg);
            }
            xSemaphoreGive(s_mutex);

            if (ok) {
                ESP_LOGI(TAG,
                         "extracted page %d (%u KB) in %lld ms (slot %d)%s",
                         target + 1, (unsigned)(jpg_size / 1024),
                         (long long)((t1 - t0) / 1000), slot,
                         still_useful ? "" : " [discarded]");
            }

            // Wake the decoder if we put fresh bytes in the cache.
            if (still_useful) xSemaphoreGive(s_decode_work_sem);
        }
    }
    s_extract_task = nullptr;
    if (s_extract_exit_sem) xSemaphoreGive(s_extract_exit_sem);
    vTaskDelete(nullptr);
}

// --- Decode worker ---

void decode_task(void*) {
    while (!s_stop) {
        if (xSemaphoreTake(s_decode_work_sem, portMAX_DELAY) != pdTRUE) continue;
        if (s_stop) break;

        while (!s_stop) {
            int byte_slot = -1;
            int target    = -1;
            int slot      = -1;
            uint8_t* jpg  = nullptr;
            size_t  jpg_size = 0;

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            byte_slot = pick_decode_byte_slot();
            if (byte_slot >= 0) {
                target   = s_bytes[byte_slot].page;
                jpg      = s_bytes[byte_slot].data;
                jpg_size = s_bytes[byte_slot].size;
                // Take ownership of the bytes — clear the byte slot
                // immediately so extract can refill it during our
                // (longer) decode pass.
                s_bytes[byte_slot].data   = nullptr;
                s_bytes[byte_slot].size   = 0;
                s_bytes[byte_slot].page   = -1;
                s_bytes[byte_slot].status = BYTES_EMPTY;

                const bool is_fg = (target == s_foreground_page);
                slot = reserve_decode_slot(target, is_fg);
                if (slot >= 0) {
                    s_slots[slot].page   = target;
                    s_slots[slot].status = SLOT_DECODING;
                } else {
                    // No decode slot reservable — discard the bytes
                    // (we'll re-extract later if still wanted).
                    target = -1;
                }
            }
            xSemaphoreGive(s_mutex);

            // Wake extract: a byte slot just opened up.
            if (byte_slot >= 0) xSemaphoreGive(s_extract_work_sem);

            if (target < 0) {
                if (jpg) std::free(jpg);
                if (byte_slot < 0) break;
                continue;  // had bytes but no decode slot — try the next byte slot
            }

            const int64_t t0 = esp_timer_get_time();
            std::memset(s_slots[slot].fb, 0xFF, s_fb_size);
            int slice_count = 1;
            const bool ok = display_jpeg_view(jpg, jpg_size, s_fit, s_slice_index,
                                              &slice_count, s_slots[slot].fb,
                                              ps3::settings::ContrastContext::Reading);
            std::free(jpg);
            const int64_t t1 = esp_timer_get_time();

            xSemaphoreTake(s_mutex, portMAX_DELAY);
            const bool still_useful = ok
                && (target == s_foreground_page || page_in_window(target));
            s_slots[slot].slice_count = slice_count;
            s_slots[slot].status = still_useful ? SLOT_READY : SLOT_EMPTY;
            xSemaphoreGive(s_mutex);

            if (ok) {
                ESP_LOGI(TAG,
                         "decoded page %d in %lld ms (slot %d)%s",
                         target + 1, (long long)((t1 - t0) / 1000), slot,
                         still_useful ? "" : " [discarded]");
            }

            xSemaphoreGive(s_done_sem);
        }
    }
    s_decode_task = nullptr;
    if (s_decode_exit_sem) xSemaphoreGive(s_decode_exit_sem);
    vTaskDelete(nullptr);
}

}  // namespace

bool start(CbzBook* book) {
    if (s_extract_task || s_decode_task) return true;

    s_book    = book;
    s_fb_size = static_cast<size_t>(epd_width()) * epd_height() / 2;

    int decode_allocated = 0;
    for (int i = 0; i < DECODE_SLOTS; ++i) {
        s_slots[i].fb = static_cast<uint8_t*>(heap_caps_malloc(
            s_fb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!s_slots[i].fb) {
            ESP_LOGE(TAG,
                     "decode slot %d alloc (%u bytes) failed",
                     i, (unsigned)s_fb_size);
            for (int j = 0; j < i; ++j) {
                std::free(s_slots[j].fb);
                s_slots[j].fb = nullptr;
            }
            return false;
        }
        s_slots[i].page   = -1;
        s_slots[i].status = SLOT_EMPTY;
        s_slots[i].slice_count = 1;
        ++decode_allocated;
    }
    for (int i = 0; i < BYTE_SLOTS; ++i) {
        s_bytes[i].data   = nullptr;
        s_bytes[i].size   = 0;
        s_bytes[i].page   = -1;
        s_bytes[i].status = BYTES_EMPTY;
    }

    s_mutex            = xSemaphoreCreateMutex();
    s_extract_work_sem = xSemaphoreCreateBinary();
    s_decode_work_sem  = xSemaphoreCreateBinary();
    s_done_sem         = xSemaphoreCreateBinary();
    s_extract_exit_sem = xSemaphoreCreateBinary();
    s_decode_exit_sem  = xSemaphoreCreateBinary();
    if (!s_mutex || !s_extract_work_sem || !s_decode_work_sem
        || !s_done_sem || !s_extract_exit_sem || !s_decode_exit_sem) {
        ESP_LOGE(TAG, "semaphore create failed");
        stop();
        return false;
    }

    s_current_page    = -1;
    s_foreground_page = -1;
    s_fit = ImageFit::Page;
    s_slice_index = 0;
    s_current_slice_count = 1;
    s_stop = false;

    if (xTaskCreatePinnedToCore(
            extract_task, "pl_extract", LOADER_TASK_STACK, nullptr,
            EXTRACT_PRIORITY, &s_extract_task, EXTRACT_CORE) != pdPASS) {
        ESP_LOGE(TAG, "extract task create failed");
        stop();
        return false;
    }
    if (xTaskCreatePinnedToCore(
            decode_task, "pl_decode", LOADER_TASK_STACK, nullptr,
            DECODE_PRIORITY, &s_decode_task, DECODE_CORE) != pdPASS) {
        ESP_LOGE(TAG, "decode task create failed");
        stop();
        return false;
    }

    ESP_LOGI(TAG,
             "page_loader started: %d byte slots (Core %d) + %d decode slots "
             "(%d fwd / %d back, Core %d) x %u KB",
             BYTE_SLOTS, EXTRACT_CORE, DECODE_SLOTS,
             PRELOAD_FORWARD, PRELOAD_BACKWARD, DECODE_CORE,
             (unsigned)(s_fb_size / 1024));
    return true;
}

void stop() {
    if (s_extract_task || s_decode_task) {
        s_stop = true;
        if (s_extract_work_sem) xSemaphoreGive(s_extract_work_sem);
        if (s_decode_work_sem)  xSemaphoreGive(s_decode_work_sem);
        // Join both workers — each may be inside an SD read or JPEG
        // decode that takes hundreds of ms. 5 s is well past worst.
        if (s_extract_exit_sem) {
            if (xSemaphoreTake(s_extract_exit_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
                ESP_LOGW(TAG, "extract worker did not exit within 5s; leaking");
            }
        }
        if (s_decode_exit_sem) {
            if (xSemaphoreTake(s_decode_exit_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
                ESP_LOGW(TAG, "decode worker did not exit within 5s; leaking");
            }
        }
    }
    if (s_mutex)            { vSemaphoreDelete(s_mutex);            s_mutex            = nullptr; }
    if (s_extract_work_sem) { vSemaphoreDelete(s_extract_work_sem); s_extract_work_sem = nullptr; }
    if (s_decode_work_sem)  { vSemaphoreDelete(s_decode_work_sem);  s_decode_work_sem  = nullptr; }
    if (s_done_sem)         { vSemaphoreDelete(s_done_sem);         s_done_sem         = nullptr; }
    if (s_extract_exit_sem) { vSemaphoreDelete(s_extract_exit_sem); s_extract_exit_sem = nullptr; }
    if (s_decode_exit_sem)  { vSemaphoreDelete(s_decode_exit_sem);  s_decode_exit_sem  = nullptr; }

    for (auto& s : s_slots) {
        if (s.fb) std::free(s.fb);
        s.fb     = nullptr;
        s.page   = -1;
        s.status = SLOT_EMPTY;
        s.slice_count = 1;
    }
    for (auto& b : s_bytes) {
        if (b.data) std::free(b.data);
        b.data   = nullptr;
        b.size   = 0;
        b.page   = -1;
        b.status = BYTES_EMPTY;
    }
    s_fb_size = 0;
    s_extract_task = nullptr;
    s_decode_task  = nullptr;
    s_book = nullptr;
}

void request(int page) {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_current_page = page;
    evict_outside_window();
    xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_extract_work_sem);
    xSemaphoreGive(s_decode_work_sem);
}

void invalidate() {
    if (!s_mutex) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    // Drop every cached page unconditionally. Decode slots keep their
    // pre-allocated framebuffers (slab-style) so we only flip status;
    // byte slots own their JPEG buffer per page so we free as we go.
    for (auto& s : s_slots) {
        s.page   = -1;
        s.status = SLOT_EMPTY;
    }
    for (auto& b : s_bytes) {
        if (b.data) std::free(b.data);
        b.data   = nullptr;
        b.size   = 0;
        b.page   = -1;
        b.status = BYTES_EMPTY;
    }
    s_foreground_page = -1;
    s_current_slice_count = 1;
    xSemaphoreGive(s_mutex);
    // Wake both workers so they refill the window for s_current_page
    // immediately rather than waiting for the next request().
    xSemaphoreGive(s_extract_work_sem);
    xSemaphoreGive(s_decode_work_sem);
}

bool try_consume(int page, uint8_t* dest_fb, size_t dest_size) {
    if (!s_mutex || !dest_fb || dest_size < s_fb_size) return false;

    bool ok = false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (auto& s : s_slots) {
        if (s.status == SLOT_READY && s.page == page) {
            std::memcpy(dest_fb, s.fb, s_fb_size);
            s_current_slice_count = s.slice_count > 0 ? s.slice_count : 1;
            // Slot stays READY (was: SLOT_EMPTY) so the just-consumed
            // page is still cached. After we update s_current_page
            // below, this slot represents "current's own slot" and
            // pick_*_target / reserve_decode_slot all keep it.
            // Quickly oscillating between page N and N+1 thus stays
            // instant — N's slot is preserved as N becomes backward.
            // Atomically clear the foreground if we just consumed it.
            // Otherwise the work_sem signals below would race the
            // foreground-clear in fetch_and_consume() and the extract
            // worker would see "foreground page is not in any cache"
            // and re-fetch the page we just took.
            if (s_foreground_page == page) s_foreground_page = -1;
            // Treat the consume as "user is now on this page" and
            // shift the preload window. Without this, main spends
            // ~480 ms in render_page's GC16/GL16 flush before calling
            // request(page), and during that gap the extract worker
            // picks the just-consumed page (still in the old window)
            // and re-extracts it for nothing.
            s_current_page = page;
            evict_outside_window();
            ok = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);

    if (ok) {
        // Slot freed — wake both workers to refill.
        xSemaphoreGive(s_extract_work_sem);
        xSemaphoreGive(s_decode_work_sem);
    }
    return ok;
}

bool fetch_and_consume(int page, uint8_t* dest_fb, size_t dest_size) {
    if (!s_mutex || !dest_fb || dest_size < s_fb_size) return false;

    if (try_consume(page, dest_fb, dest_size)) return true;

    xSemaphoreTake(s_done_sem, 0);

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_foreground_page = page;
    xSemaphoreGive(s_mutex);
    xSemaphoreGive(s_extract_work_sem);
    xSemaphoreGive(s_decode_work_sem);

    while (true) {
        // try_consume clears s_foreground_page atomically when it
        // matches, so no separate clear is needed here.
        if (try_consume(page, dest_fb, dest_size)) return true;

        if (xSemaphoreTake(s_done_sem, pdMS_TO_TICKS(5000)) != pdTRUE) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            if (s_foreground_page == page) s_foreground_page = -1;
            xSemaphoreGive(s_mutex);
            return false;
        }
    }
}

void set_view(ImageFit fit, int slice_index) {
    if (slice_index < 0) slice_index = 0;
    bool changed = false;
    if (s_mutex) xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (s_fit != fit || s_slice_index != slice_index) {
        s_fit = fit;
        s_slice_index = slice_index;
        changed = true;
    }
    if (s_mutex) xSemaphoreGive(s_mutex);
    if (changed) invalidate();
}

int current_slice_count() {
    return s_current_slice_count > 0 ? s_current_slice_count : 1;
}

}  // namespace ps3::comic::page_loader
