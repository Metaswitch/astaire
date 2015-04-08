/**
 * @file astaire.hpp - Astaire core API
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

#ifndef ASTAIRE_H__
#define ASTAIRE_H__

#include "memcachedstoreview.h"
#include "astaire_statistics.hpp"
#include "updater.h"
#include "alarm.h"

#include <string>
#include <vector>
#include <map>

class Astaire
{
public:
  Astaire(MemcachedStoreView* view,
          MemcachedConfigReader* view_cfg,
          Alarm* alarm,
          AstaireGlobalStatistics* global_stats,
          AstairePerConnectionStatistics* per_conn_stats,
          std::string self);

  ~Astaire();

  typedef std::map<std::string, std::vector<uint16_t>> TapList;
  typedef std::map<uint16_t, std::vector<std::string>> OutstandingWorkList;

  struct TapBucketsThreadData
  {
    TapBucketsThreadData(const std::string& tap_server,
                         const std::string& local_server,
                         const std::vector<uint16_t>& buckets,
                         AstaireGlobalStatistics* global_stats,
                         AstairePerConnectionStatistics::ConnectionRecord* conn_stats) :
      tap_server(tap_server),
      local_server(local_server),
      buckets(buckets),
      success(false),
      global_stats(global_stats),
      conn_stats(conn_stats)
    {}

    std::string tap_server;
    std::string local_server;
    std::vector<uint16_t> buckets;
    bool success;
    AstaireGlobalStatistics* global_stats;
    AstairePerConnectionStatistics::ConnectionRecord* conn_stats;
  };

  // Static function called by the control thread (the thread that manages the
  // work that Astaire does).
  static void* control_thread_fn(void* data);

  // Reload the cluster config and kick off a new resync operation.
  void reload_config();

  // TODO
  void trigger_full_resync();

  // Static entry point for TAP threads.  The argument must be a valid
  // TapBucketsThreadData object.  Returns the same object with the `success`
  // field updated appropriately.
  static void* tap_buckets_thread(void* data);

private:
  // Do a resync operation.  Astaire will automatically calculate the TAPs
  // required and process them to completion or failure.  This is safe to call
  // when there's nothing to do.
  //
  // @param full_resync - Whether to do a full-resync (which streams all
  //                      buckets into the local memcached from the replicas) or
  //                      a minimal-resync (which only streams vbuckets that the
  //                      local memcached does not already own).
  void do_resync(bool full_resync);

  OutstandingWorkList calculate_worklist(bool full_resync);
  void process_worklist(OutstandingWorkList& owl);
  TapList calculate_taps(const OutstandingWorkList& owl);
  bool perform_single_tap(const std::string& server,
                          const std::vector<uint16_t>& buckets,
                          pthread_t* handle);
  bool complete_single_tap(pthread_t thread_id,
                           std::string& tap_server);
  void blacklist_server(OutstandingWorkList& owl, const std::string& server);

  static uint16_t vbucket_for_key(const std::string& key);
  void handle_resync_triggers();
  static int owl_total_buckets(const OutstandingWorkList& owl);

  // Update our local copy of the memcached store view.
  //
  // @return - Whether the view was updated successfully.
  bool update_view();

  // Poll the local memcached instance to check if it is up-to-date or not
  // (whether it has been running since the last resync completed).
  //
  // @return - One of PollResult.
  enum PollResult { UP_TO_DATE, OUT_OF_DATE, ERROR };
  PollResult poll_local_memcached();

  // Tag the local memcached as being up-to-date.
  //
  // @return - Whether the tagging was successful.
  bool tag_local_memcached();

  // Untag the local memcached (so it is treated as being out-of-date).
  //
  // @return - Whether the untagging was successful.
  bool untag_local_memcached();

  // Utility function for doing a request/response cycle to the local
  // memcached.
  //
  // @param req     - The request to send. The caller retains ownership.
  // @param rsp_ptr - (out) The location to store a pointer to the received
  //                  response. The caller gains ownership of the response and
  //                  must delete it when they are finished with it. May be NULL
  //                  meaning the response is not passed out.
  //
  // @return        - Whether a response of the right type has been received.
  //
  //                  Note that this does not reflect whether the request was
  //                  actually successful, only whether we got the request to
  //                  memcached and got a sensible looking response.
  bool local_req_rsp(Memcached::BaseReq* req,
                     Memcached::BaseRsp** rsp_ptr);

  pthread_mutex_t _lock;
  pthread_cond_t _cv;

  pthread_t _control_thread;
  bool _terminated;

  Updater<void, Astaire>* _sighup_updater;
  Updater<void, Astaire>* _sigusr1_updater;

  bool _view_updated;
  MemcachedStoreView* _view;
  MemcachedConfigReader* _view_cfg;

  bool _full_resync_requested;

  Alarm* _alarm;
  AstaireGlobalStatistics* _global_stats;
  AstairePerConnectionStatistics* _per_conn_stats;

  std::string _self;
};

#endif
