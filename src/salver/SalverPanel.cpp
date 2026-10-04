#include "SalverPanel.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <XteinkDetect.h>
#include <nvs.h>

namespace salver {

namespace {

bool hasOemScreenType() {
  nvs_handle_t h;
  if (nvs_open("hw_calib", NVS_READONLY, &h) != ESP_OK) return false;
  uint8_t v = 0;
  const esp_err_t e = nvs_get_u8(h, "screenType", &v);
  nvs_close(h);
  return e == ESP_OK;
}

}  // namespace

bool resolveX4ClassicPanel() {
  if (!BoardConfig::isX4Classic() || hasOemScreenType()) return false;

  // The SDK skips this probe on the X4C on the grounds that the bus has no MISO,
  // but it reads back over the half-duplex SDA line, and an X4C V2 answers it
  // (VER=00 0F 68 00 00, FLG=13).
  uint8_t ver[5] = {0};
  uint8_t flg = 0;
  const freeink::DisplayControllerVerdict verdict = freeink::detectXteinkDisplayController(ver, &flg);
  LOG_INF("SALV", "X4C: no hw_calib/screenType; bus probe VER=%02X %02X %02X %02X %02X FLG=%02X", ver[0], ver[1],
          ver[2], ver[3], ver[4], flg);
  if (verdict != freeink::DisplayControllerVerdict::Uc81xxConfirmed) {
    // Nothing answered: an SSD1677, the SDK's default. Let the SDK keep it.
    return false;
  }

  // LUT_VER tells the UltraChip parts apart, mapped as in the SDK's own
  // promotion (XteinkDetect.cpp): 0x02/0x68/0x69 = UC8279, anything else UC8179.
  const uint8_t lutVer = ver[2];
  const bool is8279 = lutVer == 0x02 || lutVer == 0x68 || lutVer == 0x69;
  BoardConfig::ACTIVE.displayController =
      is8279 ? BoardConfig::DisplayController::UC8279 : BoardConfig::DisplayController::UC8179;
  BoardConfig::ACTIVE.displayControllerVariant = lutVer;
  LOG_INF("SALV", "X4C: panel controller %s (LUT_VER=%02X)", is8279 ? "UC8279" : "UC8179", lutVer);
  return true;
}

}  // namespace salver
