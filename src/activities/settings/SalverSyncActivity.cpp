#include "SalverSyncActivity.h"

#include <HalPowerManager.h>
#include <WiFi.h>

#include "SilentRestart.h"
#include "components/UITheme.h"
#include "salver/Salver.h"

void SalverSyncActivity::onEnter() {
  Activity::onEnter();
  message = tr(STR_SALVER_FETCHING);
  hint = tr(STR_SALVER_WAIT);
  salver::loadConfig();
  if (!salver::enabled()) {
    fetching = false;
    message = tr(STR_SALVER_NOT_CONFIGURED);
    hint = tr(STR_SALVER_CONFIG_HINT);
  }
  requestUpdate();
}

void SalverSyncActivity::onExit() {
  Activity::onExit();
  if (wifiStarted && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void SalverSyncActivity::loop() {
  if (fetching) {
    requestUpdateAndWait();
    HalPowerManager::Lock powerLock;
    wifiStarted = true;
    LOG_INF("SALV", "Manual fetch started, free heap %u", ESP.getFreeHeap());
    const auto result = salver::sync();
    LOG_INF("SALV", "Manual fetch finished (ok=%d pending=%d), free heap %u", result.ok, result.editionPending,
            ESP.getFreeHeap());
    {
      RenderLock lock(*this);
      fetching = false;
      if (result.ok) {
        message = result.editionChanged ? tr(STR_SALVER_DOWNLOADED) : tr(STR_SALVER_UP_TO_DATE);
        hint = tr(STR_SALVER_RECENT_HINT);
      } else if (result.editionPending) {
        message = tr(STR_SALVER_PENDING);
        hint = tr(STR_SALVER_PENDING_HINT);
      } else {
        message = tr(STR_SALVER_FAILED);
        hint = tr(STR_SALVER_FAILED_HINT);
      }
    }
    requestUpdate();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void SalverSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{safe.x, safe.y, safe.width, metrics.headerHeight}, tr(STR_SALVER_FETCH));
  GUI.drawHelpText(
      renderer, Rect{safe.x, safe.y + safe.height / 2 - metrics.verticalSpacing, safe.width, metrics.verticalSpacing},
      message);
  GUI.drawHelpText(
      renderer, Rect{safe.x, safe.y + safe.height / 2 + metrics.verticalSpacing, safe.width, metrics.verticalSpacing},
      hint);
  if (!fetching) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  renderer.displayBuffer();
}
