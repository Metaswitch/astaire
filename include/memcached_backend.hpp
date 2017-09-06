/**
 * @file memcached_backend.hpp
 *
 * Copyright (C) Metaswitch Networks 2016
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */


#ifndef MEMCACHEDSTORE_H__
#define MEMCACHEDSTORE_H__

#include <pthread.h>

#include <sstream>
#include <vector>

extern "C" {
#include <libmemcached/memcached.h>
#include <libmemcached/util.h>
}

#include "memcached_tap_client.hpp"
#include "memcached_config.h"
#include "memcachedstoreview.h"
#include "memcached_connection_pool.h"
#include "updater.h"
#include "sas.h"
#include "sasevent.h"
#include "communicationmonitor.h"
#include "utils.h"


class MemcachedBackend
{
public:
  MemcachedBackend(MemcachedConfigReader* config_reader,
                   BaseCommunicationMonitor* comm_monitor = NULL,
                   Alarm* vbucket_alarm = NULL);
  ~MemcachedBackend();

  /// Flags that the store should use a new view of the memcached cluster to
  /// distribute data.  Note that this is public because it is called from
  /// the MemcachedStoreUpdater class and from UT classes.
  void new_view(const MemcachedConfig& config);

  bool has_servers() { return (_servers.size() > 0); };

  /// Gets the data for the specified key.
  Memcached::ResultCode read_data(const std::string& key,
                                  std::string& data,
                                  uint64_t& cas);

  /// Sets the data for the specified key.
  Memcached::ResultCode write_data(Memcached::OpCode operation,
                                   const std::string& key,
                                   const std::string& data,
                                   uint64_t cas,
                                   int expiry);

  /// Deletes the data for the specified key.
  Memcached::ResultCode delete_data(const std::string& key);

  /// Updates the cluster settings
  void update_config();

private:
  /// Returns the vbucket for a specified key.
  int vbucket_for_key(const std::string& key);

  /// Gets the set of replica addresses to use for a read or write operation.
  typedef enum {READ, WRITE} Op;
  const std::vector<AddrInfo> get_replica_addresses(const std::string& key, Op operation);
  const std::vector<AddrInfo> get_replica_addresses(int vbucket, Op operation);

  /// Used to set the communication state for a vbucket after a get/set.
  typedef enum {OK, FAILED} CommState;
  void update_vbucket_comm_state(int vbucket, CommState state);

  // Stores time for the next vbucket alarm update. Start at 0 to ensure first update is sent
  unsigned long _next_vbucket_alarm_update = 0;
  unsigned long current_time_ms();
  // Only send alarm updates if 30 seconds have passed since last update
  unsigned int _update_period_ms = 30 * 1000;

  // Perform a get request to a single replica.
  memcached_return_t get_from_replica(memcached_st* replica,
                                      const char* key_ptr,
                                      const size_t key_len,
                                      std::string& data,
                                      uint64_t& cas);

  // Utility function to turn a return code from libmemcached back into a status
  // code that can be used in the binary protocol.
  //
  // Note that libmemcached itself only converts a subset of memcache errors to
  // distinct error codes. This is OK as the only errors we actually care about
  // are KEY_NOT_FOUND, KEY_EXISTS and ITEM_NOT_STORED, which are a subset of
  // the ones libmemcached copes with.  We convert everything else to
  // TEMPORARY_FAILURE.
  //
  // @param rc - The memcached result code to convert.
  // @return   - The corresponding memcache status code.
  static Memcached::ResultCode
    libmemcached_result_to_memcache_status(memcached_return_t rc);

  // Stores a pointer to an updater object
  Updater<void, MemcachedBackend>* _updater;

  // Used to store a pool of memcached connections to be used by worker
  // threads.
  MemcachedConnectionPool* _conn_pool;

  // Stores the number of replicas configured for the store (one means the
  // data is stored on one server, two means it is stored on two servers etc.).
  const int _replicas;

  // Stores the number of vbuckets being used.  This currently doesn't change,
  // but in future we may choose to increase it when the cluster gets
  // sufficiently large.  Note that it _must_ be a power of two.
  const int _vbuckets;

  // The options string used to create appropriate memcached_st's for the
  // current view.
  std::string _options;

  // The lock used to protect the view parameters below (_servers,
  // _read_replicas and _write_replicas).
  pthread_rwlock_t _view_lock;

  // The list of servers in this view.
  std::vector<std::string> _servers;

  // The set of read and write replicas for each vbucket.
  std::vector<std::vector<std::string> > _read_replicas;
  std::vector<std::vector<std::string> > _write_replicas;

  // The maximum expiration delta that memcached expects.  Any expiration
  // value larger than this is assumed to be an absolute rather than relative
  // value.  This matches the REALTIME_MAXDELTA constant defined by memcached.
  static const int MEMCACHED_EXPIRATION_MAXDELTA = 60 * 60 * 24 * 30;

  // Helper used to track replica communication state, and issue/clear alarms
  // based upon recent activity.
  BaseCommunicationMonitor* _comm_monitor;

  // State of last communication with replica(s) for a given vbucket, indexed
  // by vbucket.
  std::vector<CommState> _vbucket_comm_state;

  // Number of vbuckets for which the previous get/set failed to contact any
  // replicas (i.e. count of FAILED entries in _vbucket_comm_state).
  unsigned int _vbucket_comm_fail_count;

  // Lock to synchronize access to vbucket comm state accross worker threads.
  pthread_mutex_t _vbucket_comm_lock;

  // Alarms to be used for reporting vbucket inaccessible conditions.
  Alarm* _vbucket_alarm;

  // Object used to read the memcached config.
  MemcachedConfigReader* _config_reader;
};

#endif
