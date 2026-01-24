#pragma once

#include <SDCardManager.h>

#include <string>
#include <vector>

class GfxRenderer;
class Bitmap;

bool listCustomWallpapers(std::vector<std::string>& files);
bool openCustomWallpaperFile(size_t index, FsFile& file, std::string& filename);
void renderWallpaperBitmap(GfxRenderer& renderer, const Bitmap& bitmap, bool crop);
