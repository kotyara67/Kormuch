#pragma once

#include <stdint.h>

namespace SchedulePolicy {

inline uint32_t scheduledTimestamp(uint16_t dayKey, uint16_t minuteOfDay) {
  return static_cast<uint32_t>(dayKey) * 86400UL
      + static_cast<uint32_t>(minuteOfDay) * 60UL;
}

inline bool isDue(uint32_t nowTimestamp, uint16_t dayKey, uint16_t minuteOfDay,
                  uint16_t completedDay, uint8_t completedIndex,
                  uint32_t completedTimestamp, uint8_t entryIndex,
                  uint16_t graceSeconds) {
  if (minuteOfDay >= 1440U) return false;
  const uint32_t scheduled = scheduledTimestamp(dayKey, minuteOfDay);
  // Minute comparison migrates tokens written by older firmware, which stored
  // the actual start second rather than the normalized scheduled second.
  if (completedDay == dayKey && completedIndex == entryIndex
      && completedTimestamp / 60UL == scheduled / 60UL) return false;
  if (nowTimestamp < scheduled) return false;
  return static_cast<uint32_t>(nowTimestamp - scheduled) <= graceSeconds;
}

}  // namespace SchedulePolicy
