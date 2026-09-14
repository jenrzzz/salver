#include "Salver.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <sys/time.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "SalverStore.h"
#include "SalverUtil.h"
#include "WifiCredentialStore.h"
#include "util/BookCacheUtils.h"

namespace salver {
namespace {

constexpr const char* TAG = "SALV";
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr uint32_t HTTP_TIMEOUT_MS = 60000;
constexpr int64_t MIN_VALID_EPOCH = 1704067200;  // 2024-01-01: anything earlier is an unset clock
constexpr int64_t DEFAULT_NEXT_WAKE_S = 24 * 3600;
constexpr const char* SLEEP_BMP = "/sleep.bmp";
constexpr const char* SLEEP_TMP = "/.salver-sleep.tmp";

bool configLoaded = false;

// The ESP32 system clock keeps running through deep sleep, so once set it is
// the primary source. The RTC (X3) survives power loss and seeds it.
int64_t currentEpoch() {
  const time_t sys = time(nullptr);
  if (sys >= MIN_VALID_EPOCH) return sys;
  time_t rtc = 0;
  if (halClock.getEpoch(rtc) && rtc >= MIN_VALID_EPOCH) {
    timeval tv{rtc, 0};
    settimeofday(&tv, nullptr);
    return rtc;
  }
  return 0;
}

void setClock(int64_t epoch) {
  if (epoch < MIN_VALID_EPOCH) return;
  timeval tv{static_cast<time_t>(epoch), 0};
  settimeofday(&tv, nullptr);
  halClock.setEpoch(static_cast<time_t>(epoch));
}

bool tryNetwork(const WifiCredential& cred) {
  if (cred.password.empty()) {
    WiFi.begin(cred.ssid.c_str());
  } else {
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
  }
  const unsigned long start = millis();
  while (millis() - start < WIFI_TIMEOUT_MS) {
    const auto status = WiFi.status();
    if (status == WL_CONNECTED) return true;
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) return false;
    delay(100);
  }
  return false;
}

// Same recipe as WifiSelectionActivity::attemptConnection(), minus the UI.
bool connectWifi() {
  // Saved credentials are only loaded by the Wi-Fi picker (upstream #3525), so load them here.
  WIFI_STORE.loadFromFile();
  const size_t count = WIFI_STORE.getCredentialCount();
  if (count == 0) {
    LOG_ERR(TAG, "No saved Wi-Fi networks");
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    char hostname[sizeof("CrossPoint-Reader-") + 12];
    snprintf(hostname, sizeof(hostname), "CrossPoint-Reader-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    WiFi.setHostname(hostname);
  }

  // Last-connected network first, then the rest in stored order.
  std::vector<WifiCredential> candidates;
  candidates.reserve(count);
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  if (auto c = WIFI_STORE.findCredential(last)) candidates.push_back(*c);
  for (size_t i = 0; i < count; i++) {
    auto c = WIFI_STORE.getCredentialAt(i);
    if (c && c->ssid != last) candidates.push_back(*c);
  }

  for (const auto& cred : candidates) {
    LOG_INF(TAG, "Connecting to %s", cred.ssid.c_str());
    if (tryNetwork(cred)) {
      LOG_INF(TAG, "Connected, IP %s", WiFi.localIP().toString().c_str());
      return true;
    }
    WiFi.disconnect(true);
    delay(100);
  }
  LOG_ERR(TAG, "No saved network reachable");
  return false;
}

struct Fetch {
  int status = -1;  // HTTP status, or <0 for transport/file failure
  size_t bytes = 0;
  std::string etag;
  std::string edition;  // X-Salver-Edition: YYYY-MM-DD
  std::string title;    // X-Salver-Title
  int64_t now = 0;      // X-Salver-Now: server epoch seconds
  int64_t nextWakeIn = 0;
};

// Conditional GET streamed straight to `tmpPath`; a 304 (or any non-200)
// writes nothing. https works (wolfSSL, no pinning); nothing else is loaded
// on the timer-wake path, so the TLS heap is available.
Fetch conditionalGet(const std::string& url, const std::string& etag, const char* tmpPath) {
  Fetch r;
  freeink::SecureHttpClient http;
  http.setInsecure();
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    LOG_ERR(TAG, "Bad URL: %s", url.c_str());
    return r;
  }
  http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION " salver");
  if (!etag.empty()) http.addHeader("If-None-Match", etag);

  if (Storage.exists(tmpPath)) Storage.remove(tmpPath);
  HalFile out;
  bool fileError = false;
  const int status = http.GET([&](const uint8_t* data, size_t len) {
    if (http.getStatus() != 200) return true;  // drain a 304/4xx body without writing
    if (!out && !Storage.openFileForWrite(TAG, tmpPath, out)) {
      fileError = true;
      return false;
    }
    if (out.write(data, len) != len) {
      fileError = true;
      return false;
    }
    r.bytes += len;
    return true;
  });
  if (out) out.close();

  r.status = status;
  r.etag = http.getHeader("etag");
  r.edition = http.getHeader("x-salver-edition");
  r.title = http.getHeader("x-salver-title");
  r.now = parseInt64(http.getHeader("x-salver-now"), 0);
  r.nextWakeIn = parseInt64(http.getHeader("x-salver-next-wake-in"), 0);
  const bool complete = http.responseComplete();
  http.end();

  if (status == 200 && (fileError || !complete || r.bytes == 0)) {
    LOG_ERR(TAG, "Incomplete download %s (%zu bytes, file error %d)", url.c_str(), r.bytes, fileError ? 1 : 0);
    Storage.remove(tmpPath);
    r.status = -2;
  }
  LOG_INF(TAG, "GET %s -> %d (%zu bytes)", url.c_str(), r.status, r.bytes);
  return r;
}

// Delete editions dated before `cutoffDate`, never the one we just fetched.
void pruneEditions(const std::string& dir, const std::string& cutoffDate, const std::string& keepPath) {
  std::vector<std::string> victims;
  {
    HalFile d = Storage.open(dir.c_str());
    if (!d || !d.isDirectory()) return;
    char name[64];
    while (HalFile f = d.openNextFile()) {
      name[0] = '\0';
      f.getName(name, sizeof(name));
      if (editionOlderThan(name, cutoffDate.c_str())) victims.emplace_back(name);
    }
  }
  for (const auto& name : victims) {
    const std::string path = dir + "/" + name;
    if (path == keepPath) continue;
    LOG_INF(TAG, "Pruning %s", path.c_str());
    clearBookCache(path);
    Storage.remove(path.c_str());
  }
}

void scheduleNext(int64_t seconds, uint8_t failures) {
  auto& st = SALVER_STATE;
  const int64_t now = currentEpoch();
  st.failures = failures;
  st.nextWakeEpoch = now > 0 ? now + clampWakeSeconds(seconds) : 0;
  st.saveToFile();
}

SyncResult fail(const char* why, int64_t hintSeconds) {
  auto& cfg = SALVER_CONFIG;
  auto& st = SALVER_STATE;
  LOG_ERR(TAG, "Pull failed: %s", why);
  const uint8_t failures = static_cast<uint8_t>(std::min<int>(st.failures + 1, 250));
  int64_t retry = hintSeconds > 0 ? hintSeconds : cfg.retrySeconds;
  if (hintSeconds <= 0 && failures >= 4) retry = static_cast<int64_t>(cfg.retrySeconds) * 8;  // back off
  scheduleNext(retry, failures);
  return SyncResult{};
}

}  // namespace

void loadConfig() {
  configLoaded = SALVER_CONFIG.loadFromFile();
  if (configLoaded && SALVER_CONFIG.enabled()) SALVER_STATE.loadFromFile();
}

bool enabled() { return configLoaded && SALVER_CONFIG.enabled(); }

bool isTimerWake() { return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER; }

SyncResult sync() {
  SyncResult result;
  auto& cfg = SALVER_CONFIG;
  auto& st = SALVER_STATE;
  if (!enabled()) return result;

  if (!connectWifi()) return fail("no Wi-Fi", 0);
  esp_wifi_set_ps(WIFI_PS_NONE);  // modem sleep stalls long transfers (see HttpDownloader)

  const std::string base = cfg.baseUrl();
  const std::string& dir = cfg.editionsDir;
  if (!Storage.ensureDirectoryExists(dir.c_str())) return fail("editions dir", 0);
  const std::string tmp = dir + "/.salver.tmp";

  // 1. The edition.
  const Fetch epub = conditionalGet(base + "/editions/latest.epub", st.epubEtag, tmp.c_str());
  if (epub.now > 0) setClock(epub.now);
  if (epub.status == 200) {
    const std::string name = isEditionDate(epub.edition.c_str()) ? epub.edition : "latest";
    const std::string path = dir + "/" + name + ".epub";
    if (Storage.exists(path.c_str())) {
      clearBookCache(path);  // same-day rebuild: drop stale chapter cache
      Storage.remove(path.c_str());
    }
    if (!Storage.rename(tmp.c_str(), path.c_str())) return fail("rename", 0);
    st.epubEtag = epub.etag;
    st.lastEdition = path;
    // Make it the book that opens on the next button press.
    APP_STATE.openEpubPath = path;
    APP_STATE.lastSleepFromReader = true;
    APP_STATE.readerActivityLoadCount = 0;
    APP_STATE.saveToFile();
    RECENT_BOOKS.addBook(path, epub.title.empty() ? name : epub.title, "feedcurator", "");
    result.editionChanged = true;
    LOG_INF(TAG, "New edition %s", path.c_str());
  } else if (epub.status == 304) {
    LOG_INF(TAG, "Edition unchanged");
  } else if (epub.status == 404 && epub.nextWakeIn > 0) {
    // Nothing published yet; the server says when to look again.
    scheduleNext(epub.nextWakeIn, 0);
    return result;
  } else {
    return fail("edition download", epub.nextWakeIn);
  }

  // 2. The front page -> sleep screen. Not fatal.
  const Fetch bmp = conditionalGet(base + "/editions/latest.bmp", st.bmpEtag, SLEEP_TMP);
  if (bmp.status == 200) {
    if (Storage.exists(SLEEP_BMP)) Storage.remove(SLEEP_BMP);
    if (Storage.rename(SLEEP_TMP, SLEEP_BMP)) {
      st.bmpEtag = bmp.etag;
      result.sleepImageChanged = true;
      if (cfg.setSleepScreen && SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
        SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
        SETTINGS.saveToFile();
      }
    }
  } else if (bmp.status != 304) {
    LOG_ERR(TAG, "Front page download failed (%d)", bmp.status);
  }

  // 3. Next wake: the server's word, else a day.
  int64_t nextIn = epub.nextWakeIn > 0 ? epub.nextWakeIn : bmp.nextWakeIn;
  if (nextIn <= 0) nextIn = DEFAULT_NEXT_WAKE_S;
  scheduleNext(nextIn, 0);

  // 4. Housekeeping.
  const int64_t now = currentEpoch();
  if (cfg.keepDays > 0 && now > 0) {
    pruneEditions(dir, dateFromEpoch(now - static_cast<int64_t>(cfg.keepDays) * 86400), st.lastEdition);
  }

  result.ok = true;
  return result;
}

uint64_t timerWakeSeconds() {
  if (!enabled()) return 0;
  const auto& st = SALVER_STATE;
  const int64_t now = currentEpoch();
  if (st.nextWakeEpoch > 0 && now > 0) return static_cast<uint64_t>(clampWakeSeconds(st.nextWakeEpoch - now));
  return SALVER_CONFIG.fallbackSeconds;  // no clock yet: the first pull will set one
}

}  // namespace salver
