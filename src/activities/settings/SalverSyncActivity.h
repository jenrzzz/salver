#pragma once

#include <I18n.h>

#include "activities/Activity.h"

class SalverSyncActivity final : public Activity {
 public:
  explicit SalverSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SalverSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Keep the result visible after a blocking fetch exceeds the sleep timeout.
  bool preventAutoSleep() override { return true; }

 private:
  bool fetching = true;
  bool wifiStarted = false;
  const char* message = nullptr;
  const char* hint = nullptr;
};
