#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "AppVersion.h"
#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  // Brand text below the ink-drop icon. Switched from STR_CROSSINK
  // (the upstream firmware's name) to CrumBLE — our fork's identity.
  // The icon itself is a generic ink drop with no wordmark, so the
  // visual still works.
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CRUMBLE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  // Show OUR semver (CRUMBLE_VERSION from platformio.ini) rather than
  // the upstream CROSSINK_VERSION which the user has no relationship
  // with. Footer at the bottom of the screen as before.
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CRUMBLE_VERSION);
  renderer.displayBuffer();
}
