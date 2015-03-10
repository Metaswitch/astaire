#include "astaire_statistics.h"

#include <vector>
#include <string>

#include <iostream>

void AstaireGlobalStatistics::refreshed()
{
  std::vector<std::string> values;
  values.push_back(std::to_string(_total_buckets.load()));
  values.push_back(std::to_string(_resynced_bucket_count.load()));
  values.push_back(std::to_string(_resynced_keys_count.load()));
  values.push_back(std::to_string(_resynced_bytes_count.load()));
  values.push_back(std::to_string(_bandwidth));
  std::cout << "GLOBAL STATS: ";
  for (auto i = values.begin(); i != values.end(); ++i)
    std::cout << *i << "-";
  std::cout << std::endl;
  _statistic.report_change(values);
}

void AstaireGlobalStatistics::refresh(bool force)
{
  // Get the timestamp from the start of the current period, and the timestamp
  // now.
  uint_fast64_t timestamp_us = _timestamp_us.load();
  uint_fast64_t timestamp_us_now = get_timestamp_us();

  // If we're forced, or this period is already long enough, read the new
  // values and make the refreshed() callback.
  if ((timestamp_us_now >= timestamp_us + _target_period_us) &&
      (_timestamp_us.compare_exchange_weak(timestamp_us, timestamp_us_now)))
  {
    read(timestamp_us_now - timestamp_us);
    refreshed();
  }
  else if (force)
  {
    refreshed();
  }
}

void AstaireGlobalStatistics::read(uint_fast64_t period_us)
{
  uint_fast64_t period_s = period_us / (1000 * 1000);
  uint_fast64_t bandwidth_raw = _bandwidth_raw.exchange(0);
  if (period_s == 0)
  {
    _bandwidth = 0;
  }
  else
  {
    _bandwidth = bandwidth_raw / (period_s);
  }
}

void AstaireGlobalStatistics::reset()
{
  _timestamp_us.store(get_timestamp_us());
  
  // Use store(0) rather than zero_* so we don't call refresh till the end.
  _total_buckets.store(0);
  _resynced_bucket_count.store(0);
  _resynced_keys_count.store(0);
  _resynced_bytes_count.store(0);
  _bandwidth_raw.store(0);
  _bandwidth = 0;
  refresh(true);
}
