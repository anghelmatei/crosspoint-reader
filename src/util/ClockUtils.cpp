#include "ClockUtils.h"

#include <cstdio>
#include <ctime>

bool formatStatusBarClock(char* buffer, size_t bufferSize) {
  if (!buffer || bufferSize < 6) {
    return false;
  }

  const time_t now = time(nullptr);
  // 2021-01-01 00:00:00 UTC as a sanity threshold for valid time
  if (now < 1609459200) {
    return false;
  }

  struct tm timeInfo;
  localtime_r(&now, &timeInfo);
  const int written = snprintf(buffer, bufferSize, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
  return written > 0;
}
