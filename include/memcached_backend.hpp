/**
 * @file memcached_backend.hpp
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2013  Metaswitch Networks Ltd
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
#include "updater.h"
#include "sas.h"
#include "sasevent.h"
#include "communicationmonitor.h"


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

  void set_max_connect_latency(unsigned int ms);

  /// Gets the data for the specified key.
  Memcached::ResultCode read_data(const std::string& key,
                                  std::string& data,
                                  uint64_t& cas,
                                  SAS::TrailId trail = 0);

  /// Sets the data for the specified key.
  Memcached::ResultCode write_data(Memcached::OpCode operation,
                                   const std::string& key,
                                   const std::string& data,
                                   uint64_t cas,
                                   int expiry,
                                   SAS::TrailId trail = 0);

  /// Deletes the data for the specified key.
  Memcached::ResultCode delete_data(const std::string& key,
                                    SAS::TrailId trail = 0);

  /// Updates the cluster settings
  void update_config();

private:
  // A copy of this structure is maintained for each worker thread, as
  // thread local data.
  typedef struct connection
  {
    // Indicates the view number being used by this thread.  When the view
    // changes the global view number is updated and each thread switches to
    // the new view by establishing new memcached_st's.
    uint64_t view_number;

    // Contains the memcached_st's for each server.
    std::map<std::string, memcached_st*> st;

    // Contains the set of read and write replicas for each vbucket.
    std::vector<std::vector<memcached_st*> > write_replicas;
    std::vector<std::vector<memcached_st*> > read_replicas;

  } connection;

  /// Returns the vbucket for a specified key.
  int vbucket_for_key(const std::string& key);

  /// Gets the set of connections to use for a read or write operation.
  typedef enum {READ, WRITE} Op;
  const std::vector<memcached_st*>& get_replicas(const std::string& key, Op operation);
  const std::vector<memcached_st*>& get_replicas(int vbucket, Op operation);

  /// Used to set the communication state for a vbucket after a get/set.
  typedef enum {OK, FAILED} CommState;
  void update_vbucket_comm_state(int vbucket, CommState state);

  // Called by the thread-local-storage clean-up functions when a thread ends.
  static void cleanup_connection(void* p);

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

  // Used to store a connection structure for each worker thread.
  pthread_key_t _thread_local;

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

  // The current global view number.  Note that this is not protected by the
  // _view_lock.
  uint64_t _view_number;

  // The lock used to protect the view parameters below (_servers,
  // _read_replicas and _write_replicas).
  pthread_rwlock_t _view_lock;

  // The list of servers in this view.
  std::vector<std::string> _servers;

  // The time to wait before timing out a connection to memcached.
  // (This is only used during normal running - at start-of-day we use
  // a fixed 10ms time, to start up as quickly as possible).
  unsigned int _max_connect_latency_ms;

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
