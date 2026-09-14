#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

/**
 * /salver.json on the SD card root: the reader's whole configuration for the
 * morning-paper pull. Absent file = feature off. Only `url` is required.
 *
 *   { "url": "http://salver.home.amber.place",
 *     "editions_dir": "/Editions", "keep_days": 7,
 *     "retry_seconds": 1800, "fallback_seconds": 3600,
 *     "set_sleep_screen": true }
 */
class SalverConfig : public PersistableStore<SalverConfig> {
  SalverConfig() = default;
  friend class PersistableStore<SalverConfig>;

 public:
  std::string url;
  std::string editionsDir = "/Editions";
  uint16_t keepDays = 7;            // prune older editions; 0 keeps everything
  uint32_t retrySeconds = 1800;     // after a failed pull
  uint32_t fallbackSeconds = 3600;  // when no clock is known yet (first run)
  bool setSleepScreen = true;       // switch the sleep screen to /sleep.bmp once a front page arrives

  static const char* getFilePath() { return "/salver.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  bool enabled() const { return !url.empty(); }
  std::string baseUrl() const;
};

#define SALVER_CONFIG SalverConfig::getInstance()

/** /.crosspoint/salver.json: what the last pull left behind. */
class SalverState : public PersistableStore<SalverState> {
  SalverState() = default;
  friend class PersistableStore<SalverState>;

 public:
  std::string epubEtag;
  std::string bmpEtag;
  std::string lastEdition;   // SD path of the newest edition
  int64_t nextWakeEpoch = 0;  // UTC seconds; 0 = unknown
  uint8_t failures = 0;       // consecutive failed pulls

  static const char* getFilePath() { return "/.crosspoint/salver.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

#define SALVER_STATE SalverState::getInstance()
