/**
 * @file proxy_server.cpp
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
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "memcached_tap_client.hpp"
#include "proxy_server.hpp"

ProxyServer::ProxyServer(MemcachedBackend* backend) :
  _listen_sock(0),
  _backend(backend)
{
}

ProxyServer::~ProxyServer()
{
}

bool ProxyServer::start()
{
  int rc;
  uint16_t port = 11311;

  TRC_STATUS("Starting proxy server on port %d", port);

  // Create a new listening socket. Use IPv6 by default as this allows IPv4
  // connections as well.
  _listen_sock = socket(AF_INET6, SOCK_STREAM, 0);
  if (_listen_sock < 0)
  {
    TRC_ERROR("Could not create listen socket: %d, %s", _listen_sock, strerror(errno));
    return false;
  }

  // Set the SO_REUSEADDR socket option so that if we restart the kernel will
  // allow us to bind to same port we were using before.
  int enable = 1;
  rc = setsockopt(_listen_sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));

  if (rc < 0)
  {
    TRC_ERROR("Error setting SO_REUSEADDR: %d, %s", rc, strerror(errno));
    return false;
  }

  // Bind to the specified port on the any address.
  struct sockaddr_in6 bind_addr = {0};
  bind_addr.sin6_family = AF_INET6;
  bind_addr.sin6_port = htons(port);

  rc = bind(_listen_sock,
            (struct sockaddr*)&(bind_addr),
            sizeof(bind_addr));
  if (rc < 0)
  {
    TRC_ERROR("Could not bind listen socket: %d, %s", rc, strerror(errno));
    return false;
  }

  // Start listening on the socket.
  rc = listen(_listen_sock, 5);
  if (rc < 0)
  {
    TRC_ERROR("Could not listen on socket: %d, %s", rc, strerror(errno));
    return false;
  }

  // Start the listening thread.
  rc = pthread_create(&_listen_thread, NULL, listen_thread_entry_point, this);
  if (rc < 0)
  {
    TRC_ERROR("Could not start listen thread: %d", rc);
    return false;
  }

  // All is well.
  TRC_STATUS("Started proxy server");
  return true;
}

void* ProxyServer::listen_thread_entry_point(void* server_param)
{
  ProxyServer* proxy_server = (ProxyServer*)server_param;
  proxy_server->listen_thread_fn();
  return NULL;
}

void ProxyServer::listen_thread_fn()
{
  while (true)
  {
    TRC_DEBUG("Waiting for new connections");

    sockaddr_storage remote_addr;
    socklen_t addr_len = sizeof(remote_addr);

    int sock = accept(_listen_sock, (sockaddr*)&remote_addr, &addr_len);

    if (sock < 0)
    {
      // There isn't really any way to recover from accept failing. Just exit,
      // and hope that things start working when we restart.
      TRC_ERROR("Error accepting socket: %d, %s", sock, strerror(errno));
      exit(1);
    }
    else
    {
      // Work out the address of the client (for logging purposes).
      std::string addr_string;

      if (remote_addr.ss_family == AF_INET)
      {
        char buffer[100];
        inet_ntop(remote_addr.ss_family,
                  &((sockaddr_in*)&remote_addr)->sin_addr,
                  buffer,
                  sizeof(buffer));
        uint16_t port = ((sockaddr_in*)&remote_addr)->sin_port;
        addr_string.append(buffer).append(":").append(std::to_string(port));
      }
      else
      {
        char buffer[100];
        inet_ntop(remote_addr.ss_family,
                  &((sockaddr_in6*)&remote_addr)->sin6_addr,
                  buffer,
                  sizeof(buffer));
        uint16_t port = ((sockaddr_in6*)&remote_addr)->sin6_port;
        addr_string.append("[").append(buffer).append("]")
                   .append(":").append(std::to_string(port));
      }

      TRC_STATUS("Accepted socket from %s", addr_string.c_str());

      // Create a new connection, and a new thread to service it.
      Memcached::ServerConnection* connection =
        new Memcached::ServerConnection(sock, addr_string);

      ConnectionThreadParams* params = new ConnectionThreadParams;
      params->server = this;
      params->connection = connection;

      pthread_t tid;
      int rc = pthread_create(&tid,
                              NULL,
                              connection_thread_entry_point,
                              params);
      if (rc < 0)
      {
        // Couldn't create a thread to handle this connection. Just close it.
        TRC_WARNING("Could not create per-connection thread: %d", rc);
        delete connection; connection = NULL;
      }
    }
  }
}

void* ProxyServer::connection_thread_entry_point(void* params_arg)
{
  ConnectionThreadParams* params = (ConnectionThreadParams*)params_arg;
  params->server->connection_thread_fn(params->connection);
  delete params; params = NULL;
  return NULL;
}

void ProxyServer::connection_thread_fn(Memcached::ServerConnection* connection)
{
  bool keep_going = true;

  TRC_STATUS("Starting connection thread for %s", connection->address().c_str());

  while (keep_going)
  {
    Memcached::BaseMessage* msg = NULL;
    Memcached::Status status = connection->recv(&msg);

    if (status == Memcached::Status::OK)
    {
      if (msg->is_request())
      {
        Memcached::BaseReq* req = dynamic_cast<Memcached::BaseReq*>(msg);
        TRC_DEBUG("Received request with type: 0x%x", req->op_code());

        switch (req->op_code())
        {
        case (uint8_t)Memcached::OpCode::GET:
        case (uint8_t)Memcached::OpCode::GETK:
          {
            Memcached::GetReq* get_req = dynamic_cast<Memcached::GetReq*>(msg);
            handle_get(get_req, connection);
          }
          break;

        case (uint8_t)Memcached::OpCode::ADD:
        case (uint8_t)Memcached::OpCode::SET:
        case (uint8_t)Memcached::OpCode::REPLACE:
          {
            Memcached::SetAddReplaceReq* sar_req =
              dynamic_cast<Memcached::SetAddReplaceReq*>(msg);
            handle_set_add_replace(sar_req, connection);
          }
          break;

        case (uint8_t)Memcached::OpCode::DELETE:
          {
            Memcached::DeleteReq* delete_req =
              dynamic_cast<Memcached::DeleteReq*>(msg);
            handle_delete(delete_req, connection);
          }
          break;

        case (uint8_t)Memcached::OpCode::VERSION:
          {
            Memcached::VersionRsp* version_rsp =
              new Memcached::VersionRsp((uint16_t)Memcached::ResultCode::NO_ERROR,
                                        req->opaque(),
                                        "1.6.0_beta1_106_g62c7e7a");
            connection->send(*version_rsp);
            delete version_rsp; version_rsp = NULL;
          }
          break;

        default:
          {
            TRC_WARNING("Unrecognized operation: %d", req->op_code());
            keep_going = false;
          }
          break;
        }
      }
      else
      {
        // We shouldn't receive responses. Break out of the loop so we'll close
        // the connection.
        TRC_WARNING("Received unexpected response with type: 0x%x", msg->op_code());
        keep_going = false;
      }

      // We can delete the original message now.
      delete msg; msg = NULL;
    }
    else if (status == Memcached::Status::DISCONNECTED)
    {
      TRC_STATUS("Client %s has disconnected", connection->address().c_str());
      keep_going = false;
    }
    else
    {
      TRC_STATUS("Connection %s encountered an error", connection->address().c_str());
      keep_going = false;
    }
  }

  // If we fall out of the above loop for any reason, we should close the
  // connection.
  delete connection; connection = NULL;
}

void ProxyServer::handle_get(Memcached::GetReq* get_req,
                             Memcached::ServerConnection* connection)
{
  Memcached::ResultCode status;
  std::string value;
  std::string key;
  uint64_t cas;

  status = _backend->read_data(get_req->key(), value, cas, 0);

  if (get_req->response_needs_key())
  {
    key = get_req->key();
  }

  Memcached::GetRsp* get_rsp =
    new Memcached::GetRsp((uint16_t)status,
                          get_req->opaque(),
                          cas,
                          value,
                          0,
                          key);
  connection->send(*get_rsp);
  delete get_rsp; get_rsp = NULL;
}

void ProxyServer::handle_set_add_replace(Memcached::SetAddReplaceReq* sar_req,
                                         Memcached::ServerConnection* connection)
{
  Memcached::ResultCode status;

  status = _backend->write_data((Memcached::OpCode)sar_req->op_code(),
                                sar_req->key(),
                                sar_req->value(),
                                sar_req->cas(),
                                sar_req->expiry(),
                                0);

  Memcached::SetAddReplaceRsp* sar_rsp =
    new Memcached::SetAddReplaceRsp((uint8_t)sar_req->op_code(),
                                    (uint16_t)status,
                                    sar_req->opaque());
  connection->send(*sar_rsp);
  delete sar_rsp; sar_rsp = NULL;
}

void ProxyServer::handle_delete(Memcached::DeleteReq* delete_req,
                                Memcached::ServerConnection* connection)
{
  Memcached::ResultCode status;

  status = _backend->delete_data(delete_req->key(), 0);

  Memcached::DeleteRsp* delete_rsp =
    new Memcached::DeleteRsp((uint16_t)status, delete_req->opaque());

  connection->send(*delete_rsp);
  delete delete_rsp; delete_rsp = NULL;
}
