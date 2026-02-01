#include "SleepActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"
#include "images/CrossLarge.h"
#include "util/StringUtils.h"
#include "util/WallpaperUtils.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  // Skip the transient "Entering Sleep..." popup and go straight to the sleep screen.
  // This avoids an extra refresh and makes sleep/shutdown feel much faster.

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::BLANK) {
    return renderBlankSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
    return renderCustomSleepScreen();
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER) {
    return renderCoverSleepScreen();
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderPopup(const char* message) const {
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::BOLD);
  constexpr int margin = 20;
  const int x = (renderer.getScreenWidth() - textWidth - margin * 2) / 2;
  constexpr int y = 117;
  const int w = textWidth + margin * 2;
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + margin * 2;
  const bool darkMode = SETTINGS.readerDarkMode;
  // renderer.clearScreen();
  renderer.fillRect(x - 5, y - 5, w + 10, h + 10, !darkMode);
  renderer.fillRect(x + 5, y + 5, w - 10, h - 10, darkMode);
  renderer.drawText(UI_12_FONT_ID, x + margin, y + margin, message, !darkMode, EpdFontFamily::BOLD);
  renderer.displayBuffer();
}

void SleepActivity::renderCustomSleepScreen() const {
  FsFile file;
  std::vector<std::string> files;
  if (listCustomWallpapers(files)) {
    size_t selectedIndex = APP_STATE.lastSleepImage;
    if (selectedIndex >= files.size()) {
      selectedIndex = 0;
    }
    APP_STATE.lastSleepImage = selectedIndex;
    APP_STATE.saveToFile();

    const auto filename = "/sleep/" + files[selectedIndex];
    if (SdMan.openFileForRead("SLP", filename, file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderWallpaperBitmap(renderer, bitmap,
                              SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP,
                              SETTINGS.sleepScreenCoverFilter, true, HalDisplay::FAST_REFRESH);
        return;
      }
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[%lu] [SLP] Loading: /sleep.bmp\n", millis());
      renderWallpaperBitmap(renderer, bitmap,
                            SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP,
                            SETTINGS.sleepScreenCoverFilter, true, HalDisplay::FAST_REFRESH);
      return;
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(CrossLarge, (pageWidth - 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "SLEEPING");

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  renderWallpaperBitmap(renderer, bitmap,
                        SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP,
                        SETTINGS.sleepScreenCoverFilter);
}

void SleepActivity::renderCoverSleepScreen() const {
  if (APP_STATE.openEpubPath.empty()) {
    return renderDefaultSleepScreen();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtc") ||
      StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".xtch")) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      Serial.println("[SLP] Failed to load last XTC");
      return renderDefaultSleepScreen();
    }

    if (!lastXtc.generateCoverBmp()) {
      Serial.println("[SLP] Failed to generate XTC cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".txt")) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      Serial.println("[SLP] Failed to load last TXT");
      return renderDefaultSleepScreen();
    }

    if (!lastTxt.generateCoverBmp()) {
      Serial.println("[SLP] No cover image found for TXT file");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (StringUtils::checkFileExtension(APP_STATE.openEpubPath, ".epub")) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastEpub.load()) {
      Serial.println("[SLP] Failed to load last epub");
      return renderDefaultSleepScreen();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      Serial.println("[SLP] Failed to generate cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return renderDefaultSleepScreen();
  }

  FsFile file;
  if (SdMan.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      Serial.printf("[SLP] Rendering sleep cover: %s\n", coverBmpPath);
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  renderDefaultSleepScreen();
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
