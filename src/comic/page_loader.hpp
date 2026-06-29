#pragma once

#include <cstddef>
#include <cstdint>

namespace ps3::comic {

class CbzBook;

// Background loader for CBZ pages. Two FreeRTOS workers split the
// per-page work across cores: an extract worker on Core 1 reads JPEG
// bytes from the SD via miniz, and a decode worker on Core 0 turns
// those bytes into a panel-sized 4 bpp framebuffer via JPEGDEC. The
// main task keeps responding to touch while these workers fill a
// 9-slot ring of decoded pages around the user's current position
// (PRELOAD_FORWARD ahead + PRELOAD_BACKWARD behind + the current
// page itself).
//
//   ┌──────────┐  bytes      ┌──────────┐  fb       ┌─────────┐
//   │ extract  │──────────►  │ decode   │─────────► │ slot    │ ──► consumer
//   │ worker   │  ByteSlot   │ worker   │  Slot     │ ring W  │
//   └──────────┘  ring N=2   └──────────┘  ring W=9 └─────────┘
//
// Both workers prioritise a "foreground" page (the one
// fetch_and_consume is currently waiting on) over the background
// preload window, so the page the user is waiting for jumps the
// queue and is decoded next.
namespace page_loader {

// Allocate the decode-cache slots (PSRAM) and start both workers.
// `book` must outlive the loader (until stop()).
bool start(CbzBook* book);

// Stop both workers (joining via per-worker exit semaphores) and
// free the byte cache + decode cache slots.
void stop();

// Tell the loader the user is currently on `page`. The worker will
// keep the surrounding pages decoded into ring slots — the next
// PRELOAD_FORWARD pages ahead and the previous PRELOAD_BACKWARD
// pages behind, in forward-first priority. Slots whose page number
// falls outside the new window are evicted so they can be refilled.
void request(int page);

// Non-blocking: if `page` is currently in any ring slot, copy it
// into `dest_fb`, free the slot (so the worker can refill with
// page+W), and return true.
bool try_consume(int page, uint8_t* dest_fb, size_t dest_size);

// Blocking: ensure `page` is in a ring slot (decoding it as a
// foreground priority if not yet cached), then consume. Returns
// false on extract / decode failure.
bool fetch_and_consume(int page, uint8_t* dest_fb, size_t dest_size);

// Drop every cached byte / decode slot, clear the foreground tag,
// and wake both workers so they refill from scratch around the
// current page. Called when something changes that affects how a
// page is rendered (contrast curve, eventually rotation / dither
// settings) so the next consume() picks up the new pipeline output
// instead of a stale cached frame.
void invalidate();

// Number of pages kept ready ahead of and behind the user.
// Exposed for sizing estimates; not configurable at runtime.
// The total slot count includes one for the user's current page so
// that consume() doesn't drop it — without that, oscillating between
// page N and N+1 would re-decode whichever side just got consumed.
constexpr int PRELOAD_FORWARD  = 4;
constexpr int PRELOAD_BACKWARD = 4;
constexpr int DECODE_SLOTS     = PRELOAD_FORWARD + PRELOAD_BACKWARD + 1;

}  // namespace page_loader

}  // namespace ps3::comic
