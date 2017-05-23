/**
 * @file memcachedstore.cpp Memcached-backed implementation of the registration data store.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */


// Common STL includes.
#include <cassert>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <queue>
#include <string>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <time.h>

#include "log.h"
#include "utils.h"
#include "updater.h"
#include "memcachedstoreview.h"
#include "memcached_backend.hpp"


MemcachedBackend::MemcachedBackend(MemcachedConfigReader* config_reader,
                                   BaseCommunicationMonitor* comm_monitor,
                                   Alarm* vbucket_alarm) :
  _updater(NULL),
  _replicas(2),
  _vbuckets(128),
  _options(),
  _view_number(0),
  _servers(),
  _max_connect_latency_ms(50),
  _read_replicas(_vbuckets),
  _write_replicas(_vbuckets),
  _comm_monitor(comm_monitor),
  _vbucket_comm_state(_vbuckets),
  _vbucket_comm_fail_count(0),
  _vbucket_alarm(vbucket_alarm),
  _config_reader(config_reader)
{
  // Create the thread local key for the per thread data.
  pthread_key_create(&_thread_local, MemcachedBackend::cleanup_connection);

  // Create the lock for protecting the current view.
  pthread_rwlock_init(&_view_lock, NULL);

  // Create the mutex for protecting vbucket comm state.
  pthread_mutex_init(&_vbucket_comm_lock, NULL);

  // Set up the fixed options for memcached.  We use a very short connect
  // timeout because libmemcached tries to connect to all servers sequentially
  // during start-up, and if any are not up we don't want to wait for any
  // significant length of time.
  _options = "--CONNECT-TIMEOUT=10 --SUPPORT-CAS --POLL-TIMEOUT=250 --BINARY-PROTOCOL";

  // Create an updater to keep the store configured appropriately.
  _updater = new Updater<void, MemcachedBackend>(this, std::mem_fun(&MemcachedBackend::update_config));

  // Initialize vbucket comm state
  for (int ii = 0; ii < _vbuckets; ++ii)
  {
    _vbucket_comm_state[ii] = OK;
  }
}


MemcachedBackend::~MemcachedBackend()
{
  // Destroy the updater.
  delete _updater; _updater = NULL;

  // Clean up this thread's connection now, rather than waiting for
  // pthread_exit.  This is to support use by single-threaded code
  // (e.g., UTs), where pthread_exit is never called.
  connection* conn = (connection*)pthread_getspecific(_thread_local);
  if (conn != NULL)
  {
    pthread_setspecific(_thread_local, NULL);
    cleanup_connection(conn);
  }

  pthread_mutex_destroy(&_vbucket_comm_lock);

  pthread_rwlock_destroy(&_view_lock);

  pthread_key_delete(_thread_local);
}


// LCOV_EXCL_START - need real memcached to test
void MemcachedBackend::set_max_connect_latency(unsigned int ms)
{
  _max_connect_latency_ms = ms;
}

/// Set up a new view of the memcached cluster(s).  The view determines
/// how data is distributed around the cluster.
void MemcachedBackend::new_view(const MemcachedConfig& config)
{
  TRC_STATUS("Updating memcached store configuration");

  // Create a new view with the new server lists.
  MemcachedStoreView view(_vbuckets, _replicas);
  view.update(config);

  // Now copy the view so it can be accessed by the worker threads.
  pthread_rwlock_wrlock(&_view_lock);

  // Get the list of servers from the view.
  _servers = view.servers();

  // For each vbucket, get the list of read replicas and write replicas.
  for (int ii = 0; ii < _vbuckets; ++ii)
  {
    _read_replicas[ii] = view.read_replicas(ii);
    _write_replicas[ii] = view.write_replicas(ii);
  }

  // Update the view number as the last thing here, otherwise we could stall
  // other threads waiting for the lock.
  TRC_STATUS("Finished preparing new view, so flag that workers should switch to it");
  ++_view_number;

  pthread_rwlock_unlock(&_view_lock);
}


void MemcachedBackend::update_config()
{
  MemcachedConfig cfg;

  if (_config_reader->read_config(cfg))
  {
    new_view(cfg);
  }
  else
  {
    TRC_ERROR("Failed to read config, keeping previous settings");
  }
}


/// Returns the vbucket for a specified key
int MemcachedBackend::vbucket_for_key(const std::string& key)
{
  // Hash the key and convert the hash to a vbucket.
  int hash = memcached_generate_hash_value(key.data(), key.length(), MEMCACHED_HASH_MD5);
  int vbucket = hash & (_vbuckets - 1);
  TRC_DEBUG("Key %s hashes to vbucket %d via hash 0x%x", key.c_str(), vbucket, hash);
  return vbucket;
}


/// Gets the set of replicas to use for a read or write operation for the
/// specified key.
const std::vector<memcached_st*>&
MemcachedBackend::get_replicas(const std::string& key, Op operation)
{
  return get_replicas(vbucket_for_key(key), operation);
}


/// Gets the set of replicas to use for a read or write operation for the
/// specified vbucket.
const std::vector<memcached_st*>& MemcachedBackend::get_replicas(int vbucket,
                                                                   Op operation)
{
  MemcachedBackend::connection* conn = (connection*)pthread_getspecific(_thread_local);
  if (conn == NULL)
  {
    // Create a new connection structure for this thread.
    conn = new MemcachedBackend::connection;
    pthread_setspecific(_thread_local, conn);
    conn->view_number = 0;
  }

  if (conn->view_number != _view_number)
  {
    // Either the view has changed or has not yet been set up, so set up the
    // connection and replica structures for this thread.
    for (std::map<std::string, memcached_st*>::iterator it = conn->st.begin();
         it != conn->st.end();
         it++)
    {
      memcached_free(it->second);
      it->second = NULL;
    }
    pthread_rwlock_rdlock(&_view_lock);

    TRC_DEBUG("Set up new view %d for thread", _view_number);

    // Create a set of memcached_st's one per server.
    for (size_t ii = 0; ii < _servers.size(); ++ii)
    {
      // Create a new memcached_st for this server.  Do not specify the server
      // at this point as memcached() does not support IPv6 addresses.
      TRC_DEBUG("Setting up server %d for connection %p (%s)", ii, conn, _options.c_str());
      conn->st[_servers[ii]] = memcached(_options.c_str(), _options.length());
      TRC_DEBUG("Set up connection %p to server %s", conn->st[_servers[ii]], _servers[ii].c_str());

      // Switch to a longer connect timeout from here on.
      memcached_behavior_set(conn->st[_servers[ii]], MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT, _max_connect_latency_ms);

      // Disable Nagle's algorithm
      // (https://en.wikipedia.org/wiki/Nagle%27s_algorithm). If we leave it on
      // there can be up to 500ms delay between this code sending an
      // asynchronous SET and it actually being sent on the wire, e.g.
      //
      // * Ask libmemcached to do async SET.
      // * Async SET sent on the wire.
      // * Ask libmemcached to do a 2nd async SET.
      // * Up to 500ms passes.
      // * TCP stack receives ACK to 1st SET (may be delayed because the server
      //   does not send a protocol level response to the async SET).
      // * 2nd async SET sent on the wire (up to 500ms late).
      //
      // This delay can open up window conditions in failure scenarios. In
      // addition there is not much point in using Nagle. libmemcached's buffers
      // are large enough that it will never send small message fragments, and
      // this store's implementation means we very rarely pipeline requests.
      memcached_behavior_set(conn->st[_servers[ii]], MEMCACHED_BEHAVIOR_TCP_NODELAY, true);

      std::string server;
      int port;
      if (Utils::split_host_port(_servers[ii], server, port))
      {
        TRC_DEBUG("Setting server to IP address %s port %d",
                  server.c_str(),
                  port);
        memcached_server_add(conn->st[_servers[ii]], server.c_str(), port);
      }
      else
      {
        TRC_ERROR("Malformed host/port %s, skipping server", _servers[ii].c_str());
      }
    }

    conn->read_replicas.resize(_vbuckets);
    conn->write_replicas.resize(_vbuckets);

    // Now set up the read and write replica sets.
    for (int ii = 0; ii < _vbuckets; ++ii)
    {
      conn->read_replicas[ii].resize(_read_replicas[ii].size());
      for (size_t jj = 0; jj < _read_replicas[ii].size(); ++jj)
      {
        conn->read_replicas[ii][jj] = conn->st[_read_replicas[ii][jj]];
      }
      conn->write_replicas[ii].resize(_write_replicas[ii].size());
      for (size_t jj = 0; jj < _write_replicas[ii].size(); ++jj)
      {
        conn->write_replicas[ii][jj] = conn->st[_write_replicas[ii][jj]];
      }
    }

    // Flag that we are in sync with the latest view.
    conn->view_number = _view_number;

    pthread_rwlock_unlock(&_view_lock);
  }

  return (operation == Op::READ) ? conn->read_replicas[vbucket] : conn->write_replicas[vbucket];
}


/// Update state of vbucket replica communication. If alarms are configured, a set
/// alarm is issued if a vbucket becomes inaccessible, a clear alarm is issued once
/// all vbuckets become accessible again.
/// While _vbucket_comm_fail_count will essentially always be equal to the number of
/// non-OK elements in _vbucket_comm_state, the comparison to 0 is much easier using
/// an int rather than iterating over a map, so we maintain both.
void MemcachedBackend::update_vbucket_comm_state(int vbucket, CommState state)
{
  if (_vbucket_alarm)
  {
    pthread_mutex_lock(&_vbucket_comm_lock);

    if (_vbucket_comm_state[vbucket] != state)
    {
      if (state == OK)
      {
        if (_vbucket_comm_fail_count > 0)
        {
          _vbucket_comm_fail_count--;
        }
      }
      else
      {
        _vbucket_comm_fail_count++;
      }

      _vbucket_comm_state[vbucket] = state;
    }

    if (current_time_ms() > _next_vbucket_alarm_update)
    {
      if (_vbucket_comm_fail_count == 0)
      {
        _vbucket_alarm->clear();
      }
      else
      {
        _vbucket_alarm->set();
      }

      _next_vbucket_alarm_update = current_time_ms() + _update_period_ms;
    }
    pthread_mutex_unlock(&_vbucket_comm_lock);
  }
}

unsigned long MemcachedBackend::current_time_ms()
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);

  return ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

/// Called to clean up the thread local data for a thread using the
/// MemcachedBackend class.
void MemcachedBackend::cleanup_connection(void* p)
{
  MemcachedBackend::connection* conn = (MemcachedBackend::connection*)p;

  for (std::map<std::string, memcached_st*>::iterator it = conn->st.begin();
       it != conn->st.end();
       it++)
  {
    memcached_free(it->second);
    it->second = NULL;
  }

  delete conn;
}


Memcached::ResultCode MemcachedBackend::read_data(const std::string& key,
                                                  std::string& data,
                                                  uint64_t& cas)
{
  Memcached::ResultCode status = Memcached::ResultCode::NO_ERROR;

  int vbucket = vbucket_for_key(key);
  const std::vector<memcached_st*>& replicas = get_replicas(vbucket, Op::READ);

  TRC_DEBUG("%d read replicas for key %s", replicas.size(), key.c_str());

  // Read from all replicas until we get a positive result.
  memcached_return_t rc = MEMCACHED_ERROR;
  bool active_not_found = false;
  size_t failed_replicas = 0;
  size_t ii;

  // If we only have one replica, we should try it twice -
  // libmemcached won't notice a dropped TCP connection until it tries
  // to make a request on it, and will fail the request then
  // reconnect, so the second attempt could still work.
  size_t attempts = (replicas.size() == 1) ? 2 : replicas.size();

  for (ii = 0; ii < attempts; ++ii)
  {
    size_t replica_idx;

    if ((replicas.size() == 1) && (ii == 1))
    {
      if (rc != MEMCACHED_CONNECTION_FAILURE)
      {
        // This is a legitimate error, not a server failure, so we
        // shouldn't retry.
        break;
      }
      replica_idx = 0;
      TRC_WARNING("Failed to read from sole memcached replica: retrying once");
    }
    else
    {
      replica_idx = ii;
    }

    TRC_DEBUG("Attempt to read from replica %d (connection %p)",
              replica_idx,
              replicas[replica_idx]);
    rc = get_from_replica(replicas[replica_idx], key.c_str(), key.length(), data, cas);

    if (memcached_success(rc))
    {
      // Got data back from this replica. Don't try any more.
      TRC_DEBUG("Read for %s on replica %d returned SUCCESS",
                key.c_str(),
                replica_idx);
      break;
    }
    else if (rc == MEMCACHED_NOTFOUND)
    {
      // Failed to find a record on an active replica.  Flag this so if we do
      // find data on a later replica we can reset the cas value returned to
      // zero to ensure a subsequent write will succeed.
      TRC_DEBUG("Read for %s on replica %d returned NOTFOUND", key.c_str(), replica_idx);
      active_not_found = true;
    }
    else
    {
      // Error from this node, so consider it inactive.
      TRC_DEBUG("Read for %s on replica %d returned error %d (%s)",
                key.c_str(), replica_idx, rc, memcached_strerror(replicas[replica_idx], rc));
      ++failed_replicas;
    }
  }

  if (memcached_success(rc))
  {
    // Return the data and CAS value.  The CAS value is either set to the CAS
    // value from the result, or zero if an earlier active replica returned
    // NOT_FOUND.  This ensures that a subsequent set operation will succeed
    // on the earlier active replica.
    if (active_not_found)
    {
      cas = 0;
    }

    TRC_DEBUG("Read %d bytes for key %s, CAS = %ld",
              data.length(), key.c_str(), cas);
    status = Memcached::ResultCode::NO_ERROR;

    // Regardless of whether we got a tombstone, the vbucket is alive.
    update_vbucket_comm_state(vbucket, OK);

    if (_comm_monitor)
    {
      _comm_monitor->inform_success();
    }
  }
  else if (failed_replicas < replicas.size())
  {
    // At least one replica returned NOT_FOUND.
    TRC_DEBUG("At least one replica returned not found, so return NOT_FOUND");
    status = Memcached::ResultCode::KEY_NOT_FOUND;

    update_vbucket_comm_state(vbucket, OK);

    if (_comm_monitor)
    {
      _comm_monitor->inform_success();
    }
  }
  else
  {
    // All replicas returned an error, so log the error and return the
    // failure.
    TRC_ERROR("Failed to read data for %s from %d replicas",
              key.c_str(), replicas.size());

    status = Memcached::ResultCode::TEMPORARY_FAILURE;

    update_vbucket_comm_state(vbucket, FAILED);

    if (_comm_monitor)
    {
      _comm_monitor->inform_failure();
    }
  }

  return status;
}


Memcached::ResultCode MemcachedBackend::write_data(Memcached::OpCode operation,
                                                   const std::string& key,
                                                   const std::string& data,
                                                   uint64_t cas,
                                                   int expiry)
{
  if ((operation != Memcached::OpCode::ADD) &&
      (operation != Memcached::OpCode::SET) &&
      (operation != Memcached::OpCode::REPLACE))
  {
    TRC_WARNING("Unrecognized operation 0x%x, should be ADD/SET/REPLACE", operation);
    return Memcached::ResultCode::NOT_SUPPORTED;
  }

  Memcached::ResultCode status = Memcached::ResultCode::NO_ERROR;

  TRC_DEBUG("Writing %d bytes to key %s, operation = 0x%x, CAS = %ld, expiry = %d",
            data.length(), key.c_str(), operation, cas, expiry);

  int vbucket = vbucket_for_key(key);
  const std::vector<memcached_st*>& replicas = get_replicas(vbucket, Op::WRITE);

  TRC_DEBUG("%d write replicas for key %s", replicas.size(), key.c_str());

  // Calculate a timestamp (least-significant 32 bits of milliseconds since the
  // epoch) for the current time.  We store this in the flags field to allow us
  // to resolve conflicts when resynchronizing between memcached servers.
  struct timespec ts;
  (void)clock_gettime(CLOCK_REALTIME, &ts);
  uint32_t flags = (uint32_t)((ts.tv_sec * 1000) + (ts.tv_nsec / 1000000));

  // First try to write the primary data record to the first responding
  // server.
  memcached_return_t rc = MEMCACHED_ERROR;
  size_t ii;
  size_t replica_idx = 0;

  // If we only have one replica, we should try it twice -
  // libmemcached won't notice a dropped TCP connection until it tries
  // to make a request on it, and will fail the request then
  // reconnect, so the second attempt could still work.
  size_t attempts = (replicas.size() == 1) ? 2: replicas.size();

  for (ii = 0; ii < attempts; ++ii)
  {
    if ((replicas.size() == 1) && (ii == 1))
    {
      if (rc != MEMCACHED_CONNECTION_FAILURE)
      {
        // This is a legitimate error, not a transient server failure, so we
        // shouldn't retry.
        break;
      }
      replica_idx = 0;
      TRC_WARNING("Failed to write to sole memcached replica: retrying once");
    }
    else
    {
      replica_idx = ii;
    }

    TRC_DEBUG("Attempt conditional write to vbucket %d on replica %d (connection %p), CAS = %ld, expiry = %d",
              vbucket,
              replica_idx,
              replicas[replica_idx],
              cas,
              expiry);

    if (operation == Memcached::OpCode::ADD)
    {
      rc = memcached_add_vb(replicas[replica_idx],
                            key.c_str(),
                            key.length(),
                            vbucket,
                            data.data(),
                            data.length(),
                            expiry,
                            flags);
    }
    else if (operation == Memcached::OpCode::SET)
    {
      rc = memcached_set_vb(replicas[replica_idx],
                            key.c_str(),
                            key.length(),
                            vbucket,
                            data.data(),
                            data.length(),
                            expiry,
                            flags);
    }
    else  // Memcached::OpCode::REPLACE
    {
      if (cas == 0)
      {
        rc = memcached_replace_vb(replicas[replica_idx],
                                  key.c_str(),
                                  key.length(),
                                  vbucket,
                                  data.data(),
                                  data.length(),
                                  expiry,
                                  flags);
      }
      else
      {
        rc = memcached_cas_vb(replicas[replica_idx],
                              key.c_str(),
                              key.length(),
                              vbucket,
                              data.data(),
                              data.length(),
                              expiry,
                              flags,
                              cas);

        if (!memcached_success(rc))
        {
          TRC_DEBUG("memcached_cas command failed, rc = %d (%s)\n%s",
                    rc,
                    memcached_strerror(replicas[replica_idx], rc),
                    memcached_last_error_message(replicas[replica_idx]));
        }
      }
    }

    if (memcached_success(rc))
    {
      TRC_DEBUG("Conditional write succeeded to replica %d", replica_idx);
      break;
    }
    else if ((rc == MEMCACHED_NOTFOUND) ||
             (rc == MEMCACHED_NOTSTORED) ||
             (rc == MEMCACHED_DATA_EXISTS))
    {
      // A NOT_FOUND, NOT_STORED or EXISTS response indicates a concurrent write
      // failure, so return this to the application immediately - don't go on to
      // other replicas.
      TRC_INFO("Contention writing data for %s to store", key.c_str());
      status = libmemcached_result_to_memcache_status(rc);
      break;
    }
  }

  if (memcached_success(rc) && (replica_idx < replicas.size()))
  {
    // Write has succeeded, so write unconditionally (and asynchronously)
    // to the replicas.
    for (size_t jj = replica_idx + 1; jj < replicas.size(); ++jj)
    {
      TRC_DEBUG("Attempt unconditional write to replica %d", jj);
      memcached_behavior_set(replicas[jj], MEMCACHED_BEHAVIOR_NOREPLY, 1);
      memcached_set_vb(replicas[jj],
                       key.c_str(),
                       key.length(),
                       vbucket,
                       data.data(),
                       data.length(),
                       expiry,
                       flags);
      memcached_behavior_set(replicas[jj], MEMCACHED_BEHAVIOR_NOREPLY, 0);
    }
  }

  if ((!memcached_success(rc)) &&
      (rc != MEMCACHED_NOTFOUND) &&
      (rc != MEMCACHED_NOTSTORED) &&
      (rc != MEMCACHED_DATA_EXISTS))
  {
    update_vbucket_comm_state(vbucket, FAILED);

    if (_comm_monitor)
    {
      _comm_monitor->inform_failure();
    }

    TRC_ERROR("Failed to write data for %s to %d replicas",
              key.c_str(), replicas.size());

    status = Memcached::ResultCode::TEMPORARY_FAILURE;
  }
  else
  {
    update_vbucket_comm_state(vbucket, OK);

    if (_comm_monitor)
    {
      _comm_monitor->inform_success();
    }
  }

  return status;
}


Memcached::ResultCode MemcachedBackend::delete_data(const std::string& key)
{
  TRC_DEBUG("Deleting key %s", key.c_str());

  Memcached::ResultCode best_status = Memcached::ResultCode::TEMPORARY_FAILURE;

  // Delete from the read replicas - read replicas are a superset of the write
  // replicas
  const std::vector<memcached_st*>& replicas = get_replicas(key, Op::READ);
  TRC_DEBUG("Deleting from the %d read replicas for key %s",
            replicas.size(), key.c_str());

  const char* key_ptr = key.data();
  const size_t key_len = key.length();

  for (size_t ii = 0; ii < replicas.size(); ++ii)
  {
    TRC_DEBUG("Attempt delete to replica %d (connection %p)",
              ii, replicas[ii]);

    memcached_return_t rc = memcached_delete(replicas[ii],
                                             key_ptr,
                                             key_len,
                                             0);

    if (!memcached_success(rc))
    {
      TRC_ERROR("Delete failed to replica %d", ii);

      if (best_status != Memcached::ResultCode::NO_ERROR)
      {
        best_status = libmemcached_result_to_memcache_status(rc);
      }
    }
    else
    {
      best_status = Memcached::ResultCode::NO_ERROR;
    }
  }

  return best_status;
}


memcached_return_t MemcachedBackend::get_from_replica(memcached_st* replica,
                                                      const char* key_ptr,
                                                      const size_t key_len,
                                                      std::string& data,
                                                      uint64_t& cas)
{
  memcached_return_t rc = MEMCACHED_ERROR;
  cas = 0;

  // We must use memcached_mget because memcached_get does not retrieve CAS
  // values.
  rc = memcached_mget(replica, &key_ptr, &key_len, 1);

  if (memcached_success(rc))
  {
    // memcached_mget command was successful, so retrieve the result.
    TRC_DEBUG("Fetch result");
    memcached_result_st result;
    memcached_result_create(replica, &result);
    memcached_fetch_result(replica, &result, &rc);

    if (memcached_success(rc))
    {
      // Found a record, so exit the read loop.
      TRC_DEBUG("Found record on replica");

      // Copy the record into a string. std::string::assign copies its
      // arguments when used with a char*, so we can free the result
      // afterwards.
      data.assign(memcached_result_value(&result),
                  memcached_result_length(&result));
      cas = memcached_result_cas(&result);
    }

    memcached_result_free(&result);
  }

  return rc;
}

Memcached::ResultCode
MemcachedBackend::libmemcached_result_to_memcache_status(memcached_return_t rc)
{
  Memcached::ResultCode status;

  switch (rc)
  {
  case MEMCACHED_NOTFOUND:
    status = Memcached::ResultCode::KEY_NOT_FOUND;
    break;

  case MEMCACHED_DATA_EXISTS:
    status = Memcached::ResultCode::KEY_EXISTS;
    break;

  case MEMCACHED_NOTSTORED:
    status = Memcached::ResultCode::ITEM_NOT_STORED;
    break;

  default:
    status = Memcached::ResultCode::TEMPORARY_FAILURE;
    break;
  }

  return status;
}

// LCOV_EXCL_STOP
