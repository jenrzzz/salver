#pragma once

// salver: the morning paper, on a tray.
//
// A timer wakes the reader once a day; it joins the saved Wi-Fi, asks the
// feedcurator server for today's edition (an EPUB) and its front page (a
// 1-bit BMP that becomes the sleep screen), writes both to the SD card, makes
// the edition the book that opens on the next button press, and goes back to
// sleep. The device never parses a feed or a web page: it downloads exactly
// two files, with If-None-Match so unchanged mornings cost one round trip.
//
// Scheduling is the server's job. Every response carries X-Salver-Now (its
// wall clock) and X-Salver-Next-Wake-In (seconds until the next pull), so the
// reader needs no timezone and its drifting sleep timer is corrected daily.
//
// Configuration lives in /salver.json on the SD card (see SalverStore.h).
// Without configuration, automatic delivery stays disabled.

#include <cstdint>

namespace salver {

struct SyncResult {
  bool ok = false;
  bool editionChanged = false;
  bool sleepImageChanged = false;
  bool editionPending = false;
};

// Read /salver.json and the pull state after Storage.begin(), or reload before a manual fetch.
void loadConfig();

// True when /salver.json names a server.
bool enabled();

// True when this boot was caused by the deep-sleep timer (not the button).
bool isTimerWake();

// The whole paper round. Requires Wi-Fi credentials saved on the device.
SyncResult sync();

// Seconds to arm on the deep-sleep timer before sleeping, or 0 when salver is
// off. Safe to call from every sleep path; it only reads state.
uint64_t timerWakeSeconds();

}  // namespace salver
