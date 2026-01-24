#include "WallpaperUtils.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>

bool listCustomWallpapers(std::vector<std::string>& files) {
  files.clear();

  auto dir = SdMan.open("/sleep");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    file.getName(name, sizeof(name));
    auto filename = std::string(name);
    if (filename.empty() || filename[0] == '.') {
      file.close();
      continue;
    }
    if (filename.length() < 4 || filename.substr(filename.length() - 4) != ".bmp") {
      file.close();
      continue;
    }
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      file.close();
      continue;
    }
    files.emplace_back(filename);
    file.close();
  }

  dir.close();
  return !files.empty();
}

bool openCustomWallpaperFile(const size_t index, FsFile& file, std::string& filename) {
  std::vector<std::string> files;
  if (!listCustomWallpapers(files)) {
    return false;
  }

  size_t selectedIndex = index;
  if (selectedIndex >= files.size()) {
    selectedIndex = 0;
  }

  filename = "/sleep/" + files[selectedIndex];
  return SdMan.openFileForRead("WAL", filename, file);
}

void renderWallpaperBitmap(GfxRenderer& renderer, const Bitmap& bitmap, const bool crop) {
  int x = 0;
  int y = 0;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0.0f;
  float cropY = 0.0f;

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (crop) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (crop) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
  renderer.displayBuffer(EInkDisplay::HALF_REFRESH);

  if (bitmap.hasGreyscale()) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}
