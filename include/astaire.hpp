/**
 * @file astaire.hpp - Astaire core API
 *
 * Copyright (C) Metaswitch Networks
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
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

// Class that manages resyncing the local memcached node with the rest of the
// cluster. This makes use of the memcached "tap protocol" to stream records
// from other memmcached nodes, which Astaire injects into the local node.
//
// Threading Model
// ===============
//
// This class makes use of multiple threads to do its work.
//
// -  A control thread. This decides when to do a resync, what sort of resync
//    to do (see below) and what taps to set up. It also handles raising alarms
//    and PD logs.
// -  Tap threads. These are spawned by the control thread when doing a resync.
//    There is one thread per server being tapped.
// -  An updater thread that handles SIGHUP.  This updates the cluster view and
//    kicks the control thread to do a partial resync.
// -  An updater thread that handles SIGUSR1. This updates the cluster view and
//    kicks the control thread to do a full resync.
//
// All member variables of this class are protected by a lock. All member
// methods (other than the constructor and destructor) must hold this lock
// before accessing them. The public methods on this class hold the lock for as
// long as they are executing. The private methods should assume that the lock
// is held when they are called.
//
// The class also contains a condition variable to signal the control thread to
// do a resync / terminate itself.
//
// Types of Resync
// ===============
//
// Astaire can perform two types of resync:
//
// -  Minimal. Astaire only injects records in vbuckets that the local node
//    does not already own. This is used during a resize operation.
// -  Full. Astaire injects records in all vbuckets the local node should own,
//    regardless of whether it already owns them. This is used after the local
//    memcached has restarted (so it has lost all of its data), or when
//    triggered by user action.
//
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

  // Static function called by the control thread.  This simply calls
  // the `control_thread` member method.
  static void* control_thread_fn(void* data);

  // Method executed by the control thread.
  void control_thread();

  // Reload the cluster config and kick off a new resync operation.  This is
  // called when Astaire receives a SIGHUP.
  void reload_config();

  // Kick the control thread to do a full resync. This is called when Astaire
  // receives a SIGUSR1.
  //
  // This method reloads the cluster config before triggering the resync.
  void trigger_full_resync();

  // Static entry point for TAP threads.  The argument must be a valid
  // TapBucketsThreadData object.  Returns the same object with the `success`
  // field updated appropriately.
  static void* tap_buckets_thread(void* data);

private:
  void do_resync(bool full_resync);
  OutstandingWorkList calculate_worklist(bool full_resync);
  void process_worklist(OutstandingWorkList& owl);
  TapList calculate_taps(OutstandingWorkList& owl);
  bool perform_single_tap(const std::string& server,
                          const std::vector<uint16_t>& buckets,
                          pthread_t* handle);
  bool complete_single_tap(pthread_t thread_id,
                           std::string& tap_server);
  void blacklist_server(OutstandingWorkList& owl, const std::string& server);
  static int owl_total_buckets(const OutstandingWorkList& owl);
  static bool owl_empty(const OutstandingWorkList& owl);
  static uint16_t vbucket_for_key(const std::string& key);
  bool update_view();

  enum PollResult { UP_TO_DATE, OUT_OF_DATE, ERROR };
  PollResult poll_local_memcached();
  bool tag_local_memcached();
  bool untag_local_memcached();
  bool local_req_rsp(Memcached::BaseReq* req,
                     Memcached::BaseRsp** rsp_ptr);

  pthread_mutex_t _lock;
  pthread_cond_t _cv;

  pthread_t _control_thread_hdl;
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
