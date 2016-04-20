/**
 * @file proxy_server.hpp
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
