#include "SalverStore.h"

#include <Logging.h>

#include "SalverUtil.h"

void SalverConfig::toJson(JsonDocument& doc) const {
  doc["url"] = url;
  doc["editions_dir"] = editionsDir;
  doc["keep_days"] = keepDays;
  doc["retry_seconds"] = retrySeconds;
  doc["fallback_seconds"] = fallbackSeconds;
  doc["set_sleep_screen"] = setSleepScreen;
}

bool SalverConfig::fromJson(JsonVariantConst doc) {
  url = doc["url"] | "";
  editionsDir = doc["editions_dir"] | "/Editions";
  if (editionsDir.empty() || editionsDir[0] != '/') editionsDir = "/Editions";
  editionsDir = salver::stripTrailingSlash(editionsDir);
  if (editionsDir.empty()) editionsDir = "/Editions";
  keepDays = doc["keep_days"] | 7;
  retrySeconds = doc["retry_seconds"] | 1800;
  fallbackSeconds = doc["fallback_seconds"] | 3600;
  setSleepScreen = doc["set_sleep_screen"] | true;
  if (retrySeconds < 60) retrySeconds = 60;
  if (fallbackSeconds < 60) fallbackSeconds = 60;
  LOG_INF("SALV", "Config: url=%s dir=%s keep=%u", url.c_str(), editionsDir.c_str(), keepDays);
  return true;
}

std::string SalverConfig::baseUrl() const { return salver::stripTrailingSlash(url); }

void SalverState::toJson(JsonDocument& doc) const {
  doc["epub_etag"] = epubEtag;
  doc["bmp_etag"] = bmpEtag;
  doc["last_edition"] = lastEdition;
  doc["next_wake_epoch"] = nextWakeEpoch;
  doc["failures"] = failures;
}

bool SalverState::fromJson(JsonVariantConst doc) {
  epubEtag = doc["epub_etag"] | "";
  bmpEtag = doc["bmp_etag"] | "";
  lastEdition = doc["last_edition"] | "";
  nextWakeEpoch = doc["next_wake_epoch"] | 0LL;
  failures = doc["failures"] | 0;
  return true;
}
