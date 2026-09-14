#include "SalverUtil.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace salver {

bool isEditionDate(const char* s) {
  if (!s || strlen(s) != 10) return false;
  for (int i = 0; i < 10; i++) {
    if (i == 4 || i == 7) {
      if (s[i] != '-') return false;
    } else if (!isdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  return true;
}

bool editionOlderThan(const char* filename, const char* cutoffDate) {
  if (!filename || !cutoffDate || strlen(filename) != 15) return false;
  if (strcmp(filename + 10, ".epub") != 0) return false;
  char date[11];
  memcpy(date, filename, 10);
  date[10] = '\0';
  if (!isEditionDate(date) || !isEditionDate(cutoffDate)) return false;
  return strcmp(date, cutoffDate) < 0;  // ISO dates compare lexically
}

std::string dateFromEpoch(int64_t epoch) {
  const time_t t = static_cast<time_t>(epoch);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
  return buf;
}

int64_t clampWakeSeconds(int64_t seconds) {
  constexpr int64_t MIN = 60;
  constexpr int64_t MAX = 26 * 3600;
  if (seconds < MIN) return MIN;
  if (seconds > MAX) return MAX;
  return seconds;
}

int64_t parseInt64(const std::string& s, int64_t fallback) {
  if (s.empty()) return fallback;
  char* end = nullptr;
  const long long v = strtoll(s.c_str(), &end, 10);
  if (end == s.c_str()) return fallback;
  return static_cast<int64_t>(v);
}

std::string stripTrailingSlash(std::string url) {
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

}  // namespace salver
