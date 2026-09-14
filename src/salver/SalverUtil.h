#pragma once

// Pure helpers for salver (no Arduino dependencies, host-unit-testable).
#include <cstdint>
#include <string>

namespace salver {

// True for "YYYY-MM-DD" exactly (what the server sends as X-Salver-Edition).
bool isEditionDate(const char* s);

// "YYYY-MM-DD.epub" -> true when its date sorts before cutoffDate ("YYYY-MM-DD").
// Anything not named like an edition is never considered old.
bool editionOlderThan(const char* filename, const char* cutoffDate);

// UTC calendar date for an epoch, as "YYYY-MM-DD".
std::string dateFromEpoch(int64_t epoch);

// Deep-sleep timer bounds: never sooner than a minute, never longer than a
// little over a day (drift is corrected on every wake anyway).
int64_t clampWakeSeconds(int64_t seconds);

// Lenient integer parse for header values; `fallback` when empty or not a number.
int64_t parseInt64(const std::string& s, int64_t fallback);

// URL with any trailing slashes removed.
std::string stripTrailingSlash(std::string url);

}  // namespace salver
