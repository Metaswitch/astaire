#ifndef ASTAIRE_STATISTICS_H__
#define ASTAIRE_STATISTICS_H__

#include "statrecorder.h"
#include "utils.h"

#include <atomic>
#include <stdint.h>

// Macro for defining different statistics within a StatRecorder.
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

  void reset();

  GAUGE_STAT(total_buckets);
  GAUGE_STAT(resynced_bucket_count);
  GAUGE_STAT(resynced_keys_count);
  GAUGE_STAT(resynced_bytes_count);
  AVERAGE_STAT(bandwidth);

private:
  void refresh(bool force);
  void refreshed();
  void read(uint_fast64_t period_us);

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

    void refresh(bool force);
    void refreshed();
    void read(uint_fast64_t period_us);
    void reset();
    void write_out(std::vector<std::string>& vec);

    GAUGE_STAT(resynced_keys_count);
    GAUGE_STAT(resynced_bytes_count);
    AVERAGE_STAT(bandwidth);

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

    void refresh(bool force);
    void refreshed();
    void reset();
    void read(uint_fast64_t period_us);
    void write_out(std::vector<std::string>& vec);
    BucketRecord* get_bucket_stats(uint16_t bucket)
    {
      return _bucket_map[bucket];
    }

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

  ConnectionRecord* add_connection(std::string server,
                                   std::vector<uint16_t> buckets);

  void lock() { pthread_mutex_lock(&_lock); };
  void unlock() { pthread_mutex_unlock(&_lock); };

  void reset();

private:
  void refresh(bool force);
  void refreshed();
  void read(uint_fast64_t period_us);

  pthread_mutex_t _lock;
  uint_fast64_t _period_us;
  std::map<std::string, ConnectionRecord*> _connection_map;
  std::atomic_uint_fast64_t _timestamp_us;
  Statistic _statistic;
};

#endif
