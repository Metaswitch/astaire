/**
 * @file astaire_statistics.hpp - Astaire statistics API
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2015  Metaswitch Networks Ltd
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#ifndef ASTAIRE_STATISTICS_H__
#define ASTAIRE_STATISTICS_H__

#include "statrecorder.h"
#include "utils.h"

#include <atomic>
#include <stdint.h>

// Macro for defining different statistics within a StatRecorder.
//
// Gauge statistics hold values that count up and down and are reported
// accurately (e.g. not gathered over a time period and analysed to make a
// prepared value for reporting) hence we force a refresh whenever they are
// changed.
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
// Counter statistics hold values that count up as events occur and are reported
// accurately (e.g. not gathered over a time period and analysed to make a
// prepared value for reporting).  Since they change frequently, we don't force a
// refresh.
#define COUNTER_STAT(NAME)                                                      \
  public:                                                                       \
    void increment_##NAME(uint32_t delta) { _##NAME.fetch_add(delta);           \
                                            refresh(false); };                  \
    void zero_##NAME() { _##NAME.store(0); refresh(false); };                   \
    void set_##NAME(uint32_t val) { _##NAME.store(val); refresh(false); };      \
  private:                                                                      \
    std::atomic_uint_fast32_t _##NAME
// Collated statistics should only trigger reporting when their prepared value
// has been calculated (e.g. after each time period) hence we don't force the
// refreshed() call.
#define COLLATED_STAT(NAME)                                                     \
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
    _refresh_mutex(PTHREAD_MUTEX_INITIALIZER),
    _terminated(false),
    _statistic("astaire_global", lvc)
  {
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&_refresh_cond, &cond_attr);
    pthread_condattr_destroy(&cond_attr);

    int rc = pthread_create(&_refresh_thread,
                            NULL,
                            AstaireGlobalStatistics::thread_func,
                            this);
    if (rc != 0)
    {
      TRC_ERROR("Stats reporter thread creation failed (%d)", rc);
      TRC_ERROR("Stats will only be reported on change");
    }

    reset();
  };

  virtual ~AstaireGlobalStatistics()
  {
    pthread_mutex_lock(&_refresh_mutex);
    _terminated = true;
    pthread_cond_signal(&_refresh_cond);
    pthread_mutex_unlock(&_refresh_mutex);
    pthread_join(_refresh_thread, NULL);
  }

  // Entry point to run the global statistic reporting thread.  The `void*`
  // argument must be a pointer to the owning GlobalStatistics object.
  static void* thread_func(void* arg)
  {
    AstaireGlobalStatistics* glob_stats = (AstaireGlobalStatistics*)arg;
    glob_stats->thread_func();
    return NULL;
  }
  void thread_func();

  // Zero all global statistics and report that change.
  void reset();

  GAUGE_STAT(total_buckets);
  COUNTER_STAT(resynced_bucket_count);
  COUNTER_STAT(resynced_keys_count);
  COUNTER_STAT(resynced_bytes_count);
  COLLATED_STAT(bandwidth);

private:
  // Standard StatReporter API functions.
  void refresh(bool force);
  void refreshed();
  void read(uint_fast64_t period_us);

  pthread_t _refresh_thread;
  pthread_cond_t _refresh_cond;
  pthread_mutex_t _refresh_mutex;
  bool _terminated;
  std::atomic_uint_fast64_t _timestamp_us;
  Statistic _statistic;
};

class AstairePerConnectionStatistics : public StatRecorder
{
public:
  AstairePerConnectionStatistics(LastValueCache* lvc,
                                 uint_fast64_t period_us = DEFAULT_PERIOD_US) :
    StatRecorder(period_us),
    _lock(PTHREAD_MUTEX_INITIALIZER),
    _period_us(period_us),
    _statistic("astaire_connections", lvc)
  {
    lock();
    reset();
    unlock();
  }

  virtual ~AstairePerConnectionStatistics()
  {
    lock();
    reset();
    unlock();
  }

  // A record representing the stats for a single bucket.
  class ConnectionRecord;
  class BucketRecord : public StatRecorder
  {
  public:
    BucketRecord(ConnectionRecord* parent,
                 uint16_t bucket_id,
                 uint_fast64_t period_us) :
      StatRecorder(period_us),
      bucket_id(bucket_id),
      _parent(parent)
    {};
    virtual ~BucketRecord() {};

    uint16_t bucket_id;

    // All access to this object must be done while the parent ConnectionRecord (or
    // grandparent PerConnectionStatistics object) are locked.

    // Standard StatReporter API functions.
    void refresh(bool force);
    void refreshed();
    void read(uint_fast64_t period_us);
    void reset();

    // Write the stats for this BucketRecord to the given vector.
    void write_out(std::vector<std::string>& vec);

    COUNTER_STAT(resynced_keys_count);
    COUNTER_STAT(resynced_bytes_count);
    COLLATED_STAT(bandwidth);

private:
    ConnectionRecord* _parent;
  };

  // A record representing the stats for a single TAP connection.
  class ConnectionRecord : public StatRecorder
  {
  public:
    ConnectionRecord(AstairePerConnectionStatistics* parent,
                     std::string address,
                     int port,
                     std::vector<uint16_t> buckets,
                     pthread_mutex_t* lock,
                     uint_fast64_t period_us) :
      StatRecorder(period_us),
      address(address),
      port(port),
      _parent(parent),
      _period_us(period_us),
      _lock(lock)
    {
      for (std::vector<uint16_t>::const_iterator it = buckets.begin();
           it != buckets.end();
           ++it)
      {
        _bucket_map[*it] = new BucketRecord(this, *it, _period_us);
      }
      reset();
      set_total_buckets(buckets.size());
    };

    virtual ~ConnectionRecord()
    {
      for (std::map<uint16_t, BucketRecord*>::iterator it = _bucket_map.begin();
           it != _bucket_map.end();
           ++it)
      {
        delete it->second;
      }
    }

    std::string address;
    int port;

    // Called whenever a statistic is changed.  May cause statistics to be reported
    // upstream.
    //
    // The ConnectionRecord must be locked to call this function.
    void refresh(bool force);

    // No-op (required to be a StatReporter)
    void refreshed();

    // Zero all the stats for this ConenctionRecords and its child BucketRecords.
    //
    // The ConnectionRecord must be locked to call this function.
    void reset();

    // Update any collated stats in this ConnectionRecord or any of its child
    // BucketRecords.
    //
    // The ConnectionRecord must be locked to call this function.
    void read(uint_fast64_t period_us);

    // Write the stats for this ConnectionRecord to the given vector.
    //
    // The ConnectionRecord must be locked to call this function.
    void write_out(std::vector<std::string>& vec);

    // Get the BucketRecord for a given bucket ID.
    //
    // The ConnectionRecord must be locked to call this function.  The BucketRecord
    // may not be accessed after releasing the lock.
    BucketRecord* get_bucket_stats(uint16_t bucket)
    {
      return _bucket_map[bucket];
    }

    // Lock or unlock this stats object and the parent stats object.  Locking
    // is required around most public functions.
    void lock() { pthread_mutex_lock(_lock); };
    void unlock() { pthread_mutex_unlock(_lock); };

    GAUGE_STAT(total_buckets);
    GAUGE_STAT(resynced_bucket_count);

  private:
    AstairePerConnectionStatistics* _parent;
    std::map<uint16_t, BucketRecord*> _bucket_map;
    uint_fast64_t _period_us;
    pthread_mutex_t* _lock;
  };

  // Create a new ConnectionRecord to represent a singe TAP connection.
  //
  // The ConnectionRecord must be locked to call this function.
  ConnectionRecord* add_connection(std::string server,
                                   std::vector<uint16_t> buckets);

  // Lock or unlock this stats object.  Locking is required around most public
  // functions.
  void lock() { pthread_mutex_lock(&_lock); };
  void unlock() { pthread_mutex_unlock(&_lock); };

  // Zero out the statistics and report the new (empty) value.
  //
  // The ConnectionRecord must be locked to call this function.
  void reset();

private:
  void refresh(bool force);
  void refreshed();
  void read(uint_fast64_t period_us);

  pthread_mutex_t _lock;
  uint_fast64_t _period_us;
  std::vector<ConnectionRecord*> _connections;
  std::atomic_uint_fast64_t _timestamp_us;
  Statistic _statistic;
};

#endif
