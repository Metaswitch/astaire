#ifndef ASTAIRE_H__
#define ASTAIRE_H__

#include "memcachedstoreview.h"
#include "updater.h"

#include <string>
#include <vector>
#include <map>

class Astaire
{
public:
  Astaire(MemcachedStoreView* view,
          MemcachedConfigReader* view_cfg,
          std::string self) : _view(view), _view_cfg(view_cfg), _self(self)
  {
    _updater = new Updater<void, Astaire>(this,
                                          std::mem_fun(&Astaire::trigger_resync));
  };

  ~Astaire()
  {
    delete _updater;
  };

  typedef std::map<std::string, std::vector<uint16_t>> TapList;
  typedef std::map<uint16_t, std::vector<std::string>> OutstandingWorkList;

  struct TapBucketsThreadData
  {
    TapBucketsThreadData(const std::string& tap_server,
                         const std::string& local_server,
                         const std::vector<uint16_t>& buckets) :
      tap_server(tap_server),
      local_server(local_server),
      buckets(buckets),
      success(false)
    {}

    std::string tap_server;
    std::string local_server;
    std::vector<uint16_t> buckets;
    bool success;
  };

  void trigger_resync();

  static void* tap_buckets_thread(void* data);

private:
  OutstandingWorkList scaling_worklist();
  void process_worklist(OutstandingWorkList& owl);
  TapList calculate_taps(const OutstandingWorkList& owl);
  pthread_t perform_single_tap(const std::string& server,
                               const std::vector<uint16_t>& buckets);
  bool complete_single_tap(pthread_t thread_id,
                           std::string& tap_server);
  void blacklist_server(OutstandingWorkList& owl, const std::string& server);
  bool is_owl_valid(const OutstandingWorkList& owl);

  static uint16_t vbucket_for_key(const std::string& key);

  Updater<void, Astaire>* _updater;
  MemcachedStoreView* _view;
  MemcachedConfigReader* _view_cfg;
  std::string _self;
};

#endif
