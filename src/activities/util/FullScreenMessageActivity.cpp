#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "CrossPointSettings.h"
#include "fontIds.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (renderer.getScreenHeight() - height) / 2;

  const bool darkMode = SETTINGS.readerDarkMode;

  renderer.clearScreen(darkMode ? 0x00 : 0xFF);
  renderer.drawCenteredText(UI_10_FONT_ID, top, text.c_str(), !darkMode, style);
  renderer.displayBuffer(refreshMode);
}
