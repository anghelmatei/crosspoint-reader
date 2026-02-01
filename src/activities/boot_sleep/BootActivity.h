#pragma once
#include <SDCardManager.h>

#include "../Activity.h"

class BootActivity final : public Activity {
 public:
  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Boot", renderer, mappedInput) {}
  void onEnter() override;

 private:
  void renderPopup(const char* message) const;
  void renderCustomWallpaper() const;
  void renderDefaultBootScreen() const;
};
