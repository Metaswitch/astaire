#ifndef ASTAIRE_STATISTICS_H__
#define ASTAIRE_STATISTICS_H__

#include "statrecorder.h"

#include <atomic>
#include <stdint.h>

// Macro for defining a simple gauge-style statistic.
#define GAUGE_STAT(NAME)                                                        \
  public:                                                                       \
    void increment_##NAME(uint32_t delta) { _##NAME.fetch_add(delta);           \
                                            refresh(true); };                   \
    void decrement_##NAME(uint32_t delta) { _##NAME.fetch_sub(delta);           \
                                            refresh(true); };                   \
    void zero_##NAME() { _##NAME.store(0); refresh(true); };                    \
    void set_##NAME(uint32_t val) { _##NAME.store(val); refresh(true); };       \
  private:                                                                      \
    std::atomic_uint_fast32_t _##NAME
#define AVERAGE_STAT(NAME)                                                      \
  public:                                                                       \
    void increment_##NAME(uint32_t delta) { _##NAME##_raw.fetch_add(delta);     \
                                            refresh(false); };                  \
  private:                                                                      \
    std::atomic_uint_fast32_t _##NAME##_raw;                                    \
    uint32_t _##NAME

class AstaireGlobalStatistics : public StatRecorder
{
public:
  AstaireGlobalStatistics(LastValueCache* lvc,
                          uint_fast64_t period_us = DEFAULT_PERIOD_US) :
    StatRecorder(period_us),
    _statistic("astaire_global", lvc)
  {
    reset();
  };

  GAUGE_STAT(total_buckets);
  GAUGE_STAT(resynced_bucket_count);
  GAUGE_STAT(resynced_keys_count);
  GAUGE_STAT(resynced_bytes_count);
  AVERAGE_STAT(bandwidth);

  void reset();

private:
  void refresh(bool force);
  void refreshed();
  void read(uint_fast64_t period_us);

  std::atomic_uint_fast64_t _timestamp_us;
  Statistic _statistic;
};

#endif
