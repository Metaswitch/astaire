/**
 * @file proxy_server.hpp
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef PROXY_SERVER_HPP__
#define PROXY_SERVER_HPP__

#include "memcached_backend.hpp"

class ProxyServer
{
public:
  ProxyServer(MemcachedBackend* backend);
  virtual ~ProxyServer();

  /// Start the proxy server.
  ///
  /// @return - Whether the server started successfully or not.
  bool start(const char* bind_addr);

private:
  /// Entry points for the listener thread.
  static void* listen_thread_entry_point(void* server_param);
  void listen_thread_fn();

  /// Entry points for the per-connection threads.
  struct ConnectionThreadParams
  {
    ProxyServer* server;
    Memcached::ServerConnection* connection;
  };
  static void* connection_thread_entry_point(void* params);
  void connection_thread_fn(Memcached::ServerConnection* connection);

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

  /// The thread that accepts connections on the listening socket.
  pthread_t _listen_thread;

  /// The class used to access the local cluster of memcached instances.
  MemcachedBackend* _backend;
};

#endif
