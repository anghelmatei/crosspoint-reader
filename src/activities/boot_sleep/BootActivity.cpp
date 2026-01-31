#include "BootActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "CrossPointState.h"
#include "fontIds.h"
#include "images/CrossLarge.h"
#include "util/WallpaperUtils.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  // Try to show user's wallpaper with "Opening..." popup overlay
  // This mirrors SleepActivity's "Entering Sleep..." behavior
  renderCustomWallpaper();
}

void BootActivity::renderPopup(const char* message) const {
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::BOLD);
  constexpr int margin = 20;
  const int x = (renderer.getScreenWidth() - textWidth - margin * 2) / 2;
  constexpr int y = 117;
  const int w = textWidth + margin * 2;
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + margin * 2;

  // Draw popup box with border
  renderer.fillRect(x - 5, y - 5, w + 10, h + 10, true);
  renderer.fillRect(x + 5, y + 5, w - 10, h - 10, false);
  renderer.drawText(UI_12_FONT_ID, x + margin, y + margin, message, true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BootActivity::renderCustomWallpaper() const {
  // Try to load user's custom wallpaper (same one used for sleep)
  FsFile file;
  std::string filename;

  if (openCustomWallpaperFile(APP_STATE.lastSleepImage, file, filename)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      // Render the wallpaper (simplified - no grayscale on boot for speed)
      int x = 0;
      int y = 0;
      const auto pageWidth = renderer.getScreenWidth();
      const auto pageHeight = renderer.getScreenHeight();

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          x = 0;
          y = static_cast<int>((pageHeight - pageWidth / ratio) / 2);
        } else {
          x = static_cast<int>((pageWidth - pageHeight * ratio) / 2);
          y = 0;
        }
      } else {
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      renderer.clearScreen();
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight);
      file.close();

      // Overlay the "Opening..." popup
      renderPopup("Opening...");
      return;
    }
    file.close();
  }

  // Fallback to sleep.bmp in root
  if (SdMan.openFileForRead("BOOT", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x = 0;
      int y = 0;
      const auto pageWidth = renderer.getScreenWidth();
      const auto pageHeight = renderer.getScreenHeight();

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        const float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          x = 0;
          y = static_cast<int>((pageHeight - pageWidth / ratio) / 2);
        } else {
          x = static_cast<int>((pageWidth - pageHeight * ratio) / 2);
          y = 0;
        }
      } else {
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      renderer.clearScreen();
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight);
      file.close();

      renderPopup("Opening...");
      return;
    }
    file.close();
  }

  // No custom wallpaper found, use default boot screen
  renderDefaultBootScreen();
}

void BootActivity::renderDefaultBootScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(CrossLarge, (pageWidth - 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "CrossPoint", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "Opening...");
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
