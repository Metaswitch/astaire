/**
 * @file proxy_server.hpp
 *
 * Copyright (C) Metaswitch Networks 2015
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef PROXY_SERVER_HPP__
#define PROXY_SERVER_HPP__

#include "memcached_backend.hpp"
#include "threadpool.h"

class ProxyServer
{
public:
  ProxyServer(MemcachedBackend* backend,
              unsigned int num_threads,
              ExceptionHandler* exception_handler);
  virtual ~ProxyServer();

  /// Start the proxy server.
  ///
  /// @return - Whether the server started successfully or not.
  bool start(const char* bind_addr);

private:
  void listen_thread_fn();
  void connection_thread_fn(Memcached::ServerConnection* connection);

  static void inline exception_callback(std::function<void()> callable)
  {
  }

  /// Handle a GET request from the client and send an appropriate response.
  ///
  /// @param get_req    - The received request. This function does not take
  ///                     ownership.
  /// @param connection - The connection the request was received one and
  ///                     should be used for sending a response.
  void handle_get(Memcached::GetReq* get_req,
                  Memcached::ServerConnection* connection);

  /// Handle a SET/ADD/REPLACE request from the client and send an appropriate
  /// response.
  ///
  /// @param sar_req    - The received request. This function does not take
  ///                     ownership.
  /// @param connection - The connection the request was received one and
  ///                     should be used for sending a response.
  void handle_set_add_replace(Memcached::SetAddReplaceReq* sar_req,
                              Memcached::ServerConnection* connection);

  /// Handle a DELETE request from the client and send an appropriate response.
  ///
  /// @param delete_req - The received request. This function does not take
  ///                     ownership.
  /// @param connection - The connection the request was received one and
  ///                     should be used for sending a response.
  void handle_delete(Memcached::DeleteReq* delete_req,
                     Memcached::ServerConnection* connection);


  /// Socket on which the server listens for new connections.
  int _listen_sock;

  /// The class used to access the local cluster of memcached instances.
  MemcachedBackend* _backend;

  FunctorThreadPool _thread_pool;
};

#endif
