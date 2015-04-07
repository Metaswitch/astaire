/**
 * @file astaire.cpp - Astaire core function
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

#include "memcached_tap_client.hpp"
#include "astaire.hpp"
#include "astaire_pd_definitions.hpp"
#include <algorithm>

// Utility function to search a vector.
template<class T>
bool is_in_vector(const std::vector<T>& vec, const T& item)
{
  return (!(std::find(vec.begin(), vec.end(), item) == vec.end()));
}

/*****************************************************************************/
/* Public functions                                                          */
/*****************************************************************************/

Astaire::Astaire(MemcachedStoreView* view,
                 MemcachedConfigReader* view_cfg,
                 Alarm* alarm,
                 AstaireGlobalStatistics* global_stats,
                 AstairePerConnectionStatistics* per_conn_stats,
                 std::string self) :
  _terminated(false),
  _view(view),
  _view_cfg(view_cfg),
  _alarm(alarm),
  _global_stats(global_stats),
  _per_conn_stats(per_conn_stats),
  _self(self)
{
  pthread_mutex_init(&_lock, NULL);
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  pthread_cond_init(&_cv, &cond_attr);
  pthread_condattr_destroy(&cond_attr);

  // Start the controller thread.
  pthread_create(&_control_thread, NULL, control_thread_fn, this);

  // Start the updater to handle SIGHUPs
  _updater = new Updater<void, Astaire>(this,
                                        std::mem_fun(&Astaire::reload_config));
}

Astaire::~Astaire()
{
  // Destroy the updater (to stop listening for SIGHUPs).
  delete _updater; _updater = NULL;

  // Signal the controller thread to terminate.
  pthread_mutex_lock(&_lock);
  _terminated = true;
  pthread_cond_signal(&_cv);
  pthread_mutex_unlock(&_lock);

  // Now wait for the controller to exit.
  pthread_join(_control_thread, NULL);

  pthread_cond_destroy(&_cv);
  pthread_mutex_destroy(&_lock);
}

void Astaire::reload_config()
{
  MemcachedConfig conf;

  pthread_mutex_lock(&_lock);
  LOG_DEBUG("Reloading memcached config");

  if (!_view_cfg->read_config(conf))
  {
    LOG_ERROR("Invalid cluster settings file");
  }
  else
  {
    _view->update(conf);
    _view_updated = true;

    LOG_DEBUG("Signal control thread");
    pthread_cond_signal(&_cv);
  }

  pthread_mutex_unlock(&_lock);
}

void Astaire::handle_resync_triggers()
{
  pthread_mutex_lock(&_lock);

  while (!_terminated)
  {
    if (_view_updated)
    {
      // The view has been updated. Clear the flag and kick off the resync.
      _view_updated = false;
      do_resync();
    }
    else
    {
      LOG_DEBUG("Wait for view to be updated");
      pthread_cond_wait(&_cv, &_lock);
    }
  }

  pthread_mutex_unlock(&_lock);
}

// Handles the resynchronisation required given the view of the cluster.
void Astaire::do_resync()
{
  LOG_DEBUG("Start resync operation");

  OutstandingWorkList owl = scaling_worklist();
  if (owl.empty())
  {
    LOG_INFO("No scaling operation in progress, nothing to do");
    return;
  }

  _global_stats->set_total_buckets(owl.size());

  CL_ASTAIRE_START_RESYNC.log();
  if (_alarm)
  {
    _alarm->set();
  }

  process_worklist(owl);

  if (_alarm)
  {
    _alarm->clear();
  }
  CL_ASTAIRE_COMPLETE_RESYNC.log();

  _global_stats->reset();
  _per_conn_stats->reset();
}

/*****************************************************************************/
/* Static functions                                                          */
/*****************************************************************************/

void* Astaire::control_thread_fn(void* data)
{
  ((Astaire*)data)->handle_resync_triggers();
  return NULL;
}

// This thread simply performs the TAP specified in the passed object and
// updates the success flag appropriately.
void* Astaire::tap_buckets_thread(void *data)
{
  if (data == NULL)
  {
    // This must never happen.
    LOG_ERROR("Logical error - TapBucketsThreadData is NULL");
    exit(2);
  }

  // Convert argument to correct type.
  Astaire::TapBucketsThreadData* tap_data =
    (Astaire::TapBucketsThreadData*)data;

  Memcached::Connection local_conn(tap_data->local_server);
  int rc = local_conn.connect();
  if (rc != 0)
  {
    LOG_ERROR("Failed to connect to local server %s, error was (%d)",
              tap_data->local_server.c_str(),
              rc);
    return data;
  }

  Memcached::Connection tap_conn(tap_data->tap_server);
  rc = tap_conn.connect();
  if (rc != 0)
  {
    LOG_ERROR("Failed to connect to remote server %s, error was (%d)",
              tap_data->tap_server.c_str(),
              rc);
    return data;
  }

  // Assume we're going to succeed if we've got this far.
  tap_data->success = true;

  Memcached::TapConnectReq tap(tap_data->buckets);
  tap_conn.send(tap);

  bool finished = false;
  do
  {
    Memcached::BaseMessage* msg;
    Memcached::Status status = tap_conn.recv(&msg);
    if (status == Memcached::Status::ERROR)
    {
      tap_data->success = false;
      finished = true;
      break;
    }
    else if (status == Memcached::Status::DISCONNECTED)
    {
      finished = true;
      break;
    }

    if (msg->is_response())
    {
      Memcached::BaseRsp* rsp = (Memcached::BaseRsp*)msg;
      if (rsp->op_code() == (uint8_t)Memcached::OpCode::TAP_CONNECT)
      {
        // TAP_CONNECT should not be replied to, if it has, it is to
        // say that the message was not understood.
        LOG_ERROR("Cannot tap %s as the TAP protocol was not supported",
                  tap_data->tap_server.c_str());
        tap_data->success = false;
        finished = true;
      }
    }
    else
    {
      Memcached::BaseReq* req = (Memcached::BaseReq*)msg;

      if (req->op_code() == (uint8_t)Memcached::OpCode::TAP_MUTATE)
      {
        Memcached::TapMutateReq* mutate = (Memcached::TapMutateReq*)req;

        // Ths can be removed once memcached returns vbuckets on
        // TAP_MUTATE requests
        uint16_t vbucket = vbucket_for_key(mutate->key());
        LOG_DEBUG("Received TAP_MUTATE for key %s from bucket %d",
                  mutate->key().c_str(),
                  vbucket);

        std::vector<uint16_t>::iterator iter =
          std::find(tap_data->buckets.begin(),
                    tap_data->buckets.end(),
                    vbucket);
        if (iter == tap_data->buckets.end())
        {
          LOG_DEBUG("Disarding TAP_MUTATE for incorrect vBucket");
        }
        else
        {
          LOG_DEBUG("GETing record from local memcached");
          Memcached::GetReq get(mutate->key());
          local_conn.send(get);

          Memcached::BaseMessage* base_msg;
          Memcached::Status status = local_conn.recv(&base_msg);
          if (status != Memcached::Status::OK)
          {
            LOG_ERROR("Lost connection with local memcached instance");
            tap_data->success = false;
            continue;
          }

          // Check this is a Get response and cast it if so.
          if ((!base_msg->is_response()) ||
              (base_msg->op_code() != (uint8_t)Memcached::OpCode::GET))
          {
            LOG_ERROR("Received unexpected message from local memcached instance (%x)", base_msg->op_code());
            tap_data->success = false;
            delete base_msg; base_msg = NULL;
            continue;
          }
          Memcached::GetRsp* get_rsp = (Memcached::GetRsp*)base_msg;
          base_msg = NULL;

          // Examine Get response to determine whether to Add or Replace the key.
          bool do_add = false;
          bool do_replace = false;
          uint64_t cas = 0;
          if (get_rsp->result_code() == (uint8_t)Memcached::ResultCode::NO_ERROR)
          {
            // The flags field encodes a timestamp.  Calculate the difference.
            // If the timestamp in the Get response is earlier than that in the
            // Mutate, replace the value stored in the local memcached.
            if (((int32_t)get_rsp->flags()) - ((int32_t)mutate->flags()) < 0)
            {
              do_replace = true;
              cas = get_rsp->cas();
            }
          }
          else if (get_rsp->result_code() == (uint8_t)Memcached::ResultCode::KEY_NOT_FOUND)
          {
            do_add = true;
          }
          else
          {
            LOG_STATUS("Received unexpected Get response result code %x", get_rsp->result_code());
            tap_data->success = false;
            delete get_rsp; get_rsp = NULL;
            continue;
          }
          delete get_rsp; get_rsp = NULL;

          // Now actually do the Add or Replace (if required).
          if (do_add)
          {
            Memcached::AddReq add(mutate->key(),
                                  vbucket,
                                  mutate->value(),
                                  mutate->flags(),
                                  mutate->expiry());
            local_conn.send(add);

            Memcached::BaseMessage* add_rsp;
            Memcached::Status status = local_conn.recv(&add_rsp);
            if (status != Memcached::Status::OK)
            {
              LOG_ERROR("Lost connection with local memcached instance");
              tap_data->success = false;
              continue;
            }
            delete add_rsp;
          }
          else if (do_replace)
          {
            Memcached::ReplaceReq replace(mutate->key(),
                                          vbucket,
                                          mutate->value(),
                                          cas,
                                          mutate->flags(),
                                          mutate->expiry());
            local_conn.send(replace);

            Memcached::BaseMessage* replace_rsp;
            Memcached::Status status = local_conn.recv(&replace_rsp);
            if (status != Memcached::Status::OK)
            {
              LOG_ERROR("Lost connection with local memcached instance");
              tap_data->success = false;
              continue;
            }
            delete replace_rsp;
          }

          // Update global and local stats
          tap_data->global_stats->increment_resynced_keys_count(1);
          uint32_t bytes = mutate->to_wire().size();
          tap_data->global_stats->increment_resynced_bytes_count(bytes);
          tap_data->global_stats->increment_bandwidth(bytes);

          tap_data->conn_stats->lock();
          AstairePerConnectionStatistics::BucketRecord* bucket_stats =
            tap_data->conn_stats->get_bucket_stats(vbucket);
          bucket_stats->increment_resynced_keys_count(1);
          bucket_stats->increment_resynced_bytes_count(bytes);
          bucket_stats->increment_bandwidth(bytes);
          tap_data->conn_stats->unlock();
        }
      }
    }

    delete msg; msg = NULL;
  }
  while (!finished);

  if (tap_data->success)
  {
    tap_data->global_stats->set_resynced_bucket_count(tap_data->buckets.size());
    tap_data->conn_stats->lock();
    tap_data->conn_stats->set_resynced_bucket_count(tap_data->buckets.size());
    tap_data->conn_stats->unlock();
  }

  // Tidy up
  local_conn.disconnect();
  tap_conn.disconnect();

  return (void*)tap_data;
}

/*****************************************************************************/
/* Private functions                                                         */
/*****************************************************************************/

// Calculate the OWL for handling a scale operation.
//
// Builds and returns an OWL from the memcached store view (will be empty if
// there's no scale operation ongoing).
Astaire::OutstandingWorkList Astaire::scaling_worklist()
{
  OutstandingWorkList owl;
  const std::map<int, MemcachedStoreView::ReplicaChange> changes =
    _view->calculate_vbucket_moves();

  for (std::map<int, MemcachedStoreView::ReplicaChange>::const_iterator it =
         changes.begin();
       it != changes.end();
       ++it)
  {
    const MemcachedStoreView::ReplicaList& old_replicas = it->second.first;
    const MemcachedStoreView::ReplicaList& new_replicas = it->second.second;
    if (is_in_vector(old_replicas, _self))
    {
      LOG_DEBUG("Bucket (%d) is already owned by local memcached", it->first);
    }
    else if (!is_in_vector(new_replicas, _self))
    {
      LOG_DEBUG("Bucket (%d) is not owned by local memcached", it->first);
    }
    else
    {
      LOG_DEBUG("Local memcached is gaining bucket (%d)", it->first);
      owl[it->first] = old_replicas;
    }
  }

  return owl;
}

// The core of Astaire's work.  This function iterates around the OWL,
// attempting to fetch vbuckets from replicas until either all vbuckets have
// been successfully synched, or there are no replicas left for a bucket.
void Astaire::process_worklist(OutstandingWorkList& owl)
{
  // Since some servers may be unreachable, we loop over the OWL until
  // we've completely suceeded or cannot make any more progress.
  while (!owl.empty() && is_owl_valid(owl))
  {
    TapList taps = calculate_taps(owl);
    std::vector<pthread_t> tap_handles;
    tap_handles.reserve(taps.size());
    for (TapList::iterator taps_it = taps.begin();
         taps_it != taps.end();
         ++taps_it)
    {
      // Kick off a TAP on this server.
      pthread_t handle;
      bool rc = perform_single_tap(taps_it->first, taps_it->second, &handle);
      if (rc)
      {
        tap_handles.push_back(handle);
      }
    }

    for (std::vector<pthread_t>::iterator handle_it = tap_handles.begin();
         handle_it != tap_handles.end();
         ++handle_it)
    {
      // Wait for the TAP to complete and update the OWL appropriately.
      std::string server;
      bool success = complete_single_tap(*handle_it, server);
      if (success)
      {
        // Clear the tapped buckets from the OWL as they've been successfully
        // tapped.
        for (std::vector<uint16_t>::const_iterator bucket_it = taps[server].begin();
             bucket_it != taps[server].end();
             ++bucket_it)
        {
          owl.erase(*bucket_it);
        }
      }
      else
      {
        // Remove this unreachable server from the OWL.
        blacklist_server(owl, server);
      }
    }
  }

  if (!is_owl_valid(owl))
  {
    LOG_ERROR("Failed to stream some buckets");
    CL_ASTAIRE_RESYNC_FAILED.log();
  }
}

// Convert an OWL into a list of TAPs to perform.  This algorithm choses the
// first available server for each bucket.
//
// This function assumes the provided OWL is valid (you can use is_owl_valid to
// check this).
Astaire::TapList Astaire::calculate_taps(const OutstandingWorkList& owl)
{
  TapList tl;

  for (OutstandingWorkList::const_iterator it = owl.begin();
       it != owl.end();
       ++it)
  {
    std::string tapped_server = it->second[0];
    tl[tapped_server].push_back(it->first);
  }

  return tl;
}

// Kick off a tap of a single server for the given vBuckets.
//
// On success, returns the handle of the thread being used to process the
// tap.  Calling code can wait for this thread to complete by calling
// `complete_single_tap`.
bool Astaire::perform_single_tap(const std::string& server,
                                 const std::vector<uint16_t>& buckets,
                                 pthread_t* handle)
{
  _per_conn_stats->lock();
  AstairePerConnectionStatistics::ConnectionRecord* conn_stat =
    _per_conn_stats->add_connection(server, buckets);
  _per_conn_stats->unlock();

  TapBucketsThreadData* thread_data = new TapBucketsThreadData(server,
                                                               _self,
                                                               buckets,
                                                               _global_stats,
                                                               conn_stat);
  LOG_INFO("Starting TAP of %s", server.c_str());
  int rc = pthread_create(handle, NULL, tap_buckets_thread, (void*)thread_data);
  if (rc != 0)
  {
    LOG_ERROR("Failed to create TAP thread (%d)", rc);
    return false;
  }
  return true;
}

// Wait for a single TAP to complete.
//
// The return value of this function indicates whether the TAP succeeded or
// failed.  The `tap_server` parameter is set to the identity of the tapped
// server if the tap was successful.
bool Astaire::complete_single_tap(pthread_t thread_id, std::string& tap_server)
{
  TapBucketsThreadData* thread_data = NULL;
  int rc = pthread_join(thread_id, (void**)&thread_data);

  if (rc != 0)
  {
    LOG_ERROR("Failed to join TAP thread (%d)", rc);
    return false;
  }

  if (thread_data == NULL)
  {
    LOG_ERROR("Logical Error: TAP thread returned NULL thread data");
    exit(2);
  }

  tap_server = thread_data->tap_server;
  bool success = thread_data->success;
  delete thread_data; thread_data = NULL;
  return success;
}

// Remove an unreachable server from all records in the provided OWL.
void Astaire::blacklist_server(OutstandingWorkList& owl,
                               const std::string& server)
{
  for (OutstandingWorkList::iterator owl_it = owl.begin();
       owl_it != owl.end();
       ++owl_it)
  {
    std::vector<std::string> new_server_list;
    for (std::vector<std::string>::iterator it = owl_it->second.begin();
         it != owl_it->second.end();
         ++it)
    {
      if (*it != server)
      {
        new_server_list.push_back(*it);
      }
    }

    // Note that this is safe to do while iterating since we're not creating
    // a new element in the map (guaranteed safe by C++).
    owl[owl_it->first] = new_server_list;
  }
}

// Check that the provided OWL is valid (i.e. all buckets have at least one
// available replica).
bool Astaire::is_owl_valid(const OutstandingWorkList& owl)
{
  for (OutstandingWorkList::const_iterator it = owl.begin();
       it != owl.end();
       ++it)
  {
    if (it->second.empty())
    {
      return false;
    }
  }
  return true;
}

// Must match the same function in https://github.com/Metaswitch/cpp-common/blob/master/src/memcachedstore.cpp.
//
// Should be removed once memcached can supply vbuckets on the TAP protocol.
#include "libmemcached/memcached.h"
uint16_t Astaire::vbucket_for_key(const std::string& key)
{
  // Hash the key and convert the hash to a vbucket.
  int hash = memcached_generate_hash_value(key.data(),
                                           key.length(),
                                           MEMCACHED_HASH_MD5);
  int vbucket = hash & (128 - 1);
  return vbucket;
}
