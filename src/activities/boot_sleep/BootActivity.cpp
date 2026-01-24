#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"
#include "images/CrossLarge.h"
#include "util/WallpaperUtils.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    std::vector<std::string> files;
    if (listCustomWallpapers(files)) {
      size_t selectedIndex = APP_STATE.lastSleepImage;
      if (selectedIndex >= files.size()) {
        selectedIndex = 0;
      }
      APP_STATE.lastSleepImage = selectedIndex;
      APP_STATE.saveToFile();

      const auto filename = "/sleep/" + files[selectedIndex];
      FsFile file;
      if (SdMan.openFileForRead("BOT", filename, file)) {
        Bitmap bitmap(file, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderWallpaperBitmap(renderer, bitmap,
                                SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP);
          return;
        }
      }
    }

    FsFile file;
    if (SdMan.openFileForRead("BOT", "/sleep.bmp", file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderWallpaperBitmap(renderer, bitmap,
                              SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP);
        return;
      }
    }
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(CrossLarge, (pageWidth + 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "BOOTING");
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
