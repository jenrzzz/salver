#pragma once

namespace salver {

// X4 Classic panel fallback. The SDK picks the X4C panel controller from the
// factory NVS key hw_calib/screenType alone and assumes SSD1677 when it is
// missing, but some X4C V2 units ship with no hw_calib namespace at all and a
// UC8179 panel, which then never refreshes. When the key is missing this probes
// the display bus instead and sets BoardConfig::ACTIVE.displayController.
// Returns true when it resolved the controller, so the caller skips
// freeink::applyXteinkDisplayController(). Returns false on every other board,
// and whenever the key is present. Call before display.begin().
bool resolveX4ClassicPanel();

}  // namespace salver
