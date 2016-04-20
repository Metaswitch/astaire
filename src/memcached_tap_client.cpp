/**
 * @file memcached_tap_client.cpp
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
#include "utils.h"

#include <cstring>
#include <cassert>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>

void Memcached::Utils::write(const std::string& str, std::string& ss)
{
  ss.append(str);
}

bool Memcached::is_msg_complete(const std::string& msg,
                                bool& request,
                                uint16_t& body_length,
                                uint8_t& op_code)
{
  uint32_t raw_length = msg.length();
  const char* raw = msg.data();

  if (raw_length < sizeof(MsgHdr))
  {
    // Too short
    return false;
  }

  if (raw[0] == (char)0x80)
  {
    request = true;
  }
  else
  {
    request = false;
  }

  // Overlay the message data with the header structure to determine the full
  // length.
  body_length = HDR_GET(raw, body_length);

  if (raw_length < sizeof(MsgHdr) + body_length)
  {
    // Too short after all
    return false;
  }

  // This is a complete message, get the op-code out too.
  op_code = HDR_GET(raw, op_code);

  return true;
}

bool Memcached::from_wire(std::string& msg,
                          Memcached::BaseMessage*& output)
{
  bool request;
  uint16_t body_length;
  uint8_t op_code;

  if (!is_msg_complete(msg, request, body_length, op_code))
  {
    // Need more data.
    return false;
  }

  if (request)
  {
    switch (op_code)
    {
    case (uint8_t)OpCode::TAP_MUTATE:
      output = from_wire_int<Memcached::TapMutateReq>(msg);
      break;
    case (uint8_t)OpCode::GET:
    case (uint8_t)OpCode::GETK:
      output = from_wire_int<Memcached::GetReq>(msg);
      break;
    case (uint8_t)OpCode::SET:
      output = from_wire_int<Memcached::SetReq>(msg);
      break;
    case (uint8_t)OpCode::ADD:
      output = from_wire_int<Memcached::AddReq>(msg);
      break;
    case (uint8_t)OpCode::REPLACE:
      output = from_wire_int<Memcached::ReplaceReq>(msg);
      break;
    case (uint8_t)OpCode::DELETE:
      output = from_wire_int<Memcached::DeleteReq>(msg);
      break;
    case (uint8_t)OpCode::VERSION:
      output = from_wire_int<Memcached::VersionReq>(msg);
      break;
    default:
      output = from_wire_int<Memcached::BaseReq>(msg);
      break;
    }
  }
  else
  {
    switch (op_code)
    {
    case (uint8_t)OpCode::GET:
      output = Memcached::from_wire_int<Memcached::GetRsp>(msg);
      break;
    case (uint8_t)OpCode::ADD:
      output = Memcached::from_wire_int<Memcached::AddRsp>(msg);
      break;
    case (uint8_t)OpCode::REPLACE:
      output = Memcached::from_wire_int<Memcached::ReplaceRsp>(msg);
      break;
    default:
      output = Memcached::from_wire_int<Memcached::BaseRsp>(msg);
      break;
    }
  }

  // And finally trim the message from the start of the string.
  msg = msg.substr(sizeof(MsgHdr) + body_length, std::string::npos);

  return true;
}

std::string Memcached::BaseMessage::to_wire() const
{
  std::string ss;

  // Build the message-specific sections.
  std::string extra = generate_extra();
  std::string value = generate_value();
  uint16_t vbucket_or_status = generate_vbucket_or_status();

  // Calculate body size, this is the sum of the sizes of Extras, Key and
  // Values sections.
  uint32_t body_size = extra.length() + _key.length() + value.length();

  // In the memcache protocol the first byte (aka the "magic" byte) is 0x80 for
  // a request and 0x81 for a response.
  uint8_t magic_byte = is_request() ? 0x80 : 0x81;
  Utils::write(magic_byte, ss);
  Utils::write((uint8_t)_op_code, ss);
  Utils::write((uint16_t)_key.length(), ss);
  Utils::write((uint8_t)extra.length(), ss);
  Utils::write((uint8_t)0x00, ss); // Data Type (0x00 - RAW_DATA)
  Utils::write((uint16_t)vbucket_or_status, ss);
  Utils::write((uint32_t)body_size, ss);
  Utils::write((uint32_t)_opaque, ss);
  Utils::write((uint64_t)_cas, ss);
  Utils::write(extra, ss);
  Utils::write(_key, ss);
  Utils::write(value, ss);

  return ss;
}

Memcached::BaseMessage::BaseMessage(const std::string& msg)
{
  const char* raw = msg.data();
  _op_code = HDR_GET(raw, op_code);
  _opaque = HDR_GET(raw, opaque);
  _cas = HDR_GET(raw, cas);
  _key = msg.substr(sizeof(MsgHdr) + HDR_GET(raw, extra_length),
                    HDR_GET(raw, key_length));
}

Memcached::BaseReq::BaseReq(const std::string& msg) : BaseMessage(msg)
{
  _vbucket = HDR_GET(msg.data(), vbucket_or_status);
}

Memcached::BaseRsp::BaseRsp(const std::string& msg) : BaseMessage(msg)
{
  _status = HDR_GET(msg.data(), vbucket_or_status);
}

bool Memcached::GetReq::response_needs_key() const
{
  return (_op_code == (uint8_t)OpCode::GETK);
}

Memcached::GetRsp::GetRsp(const std::string& msg) : BaseRsp(msg)
{
  const char* raw = msg.data();
  uint16_t key_length = HDR_GET(raw, key_length);
  uint8_t extra_length = HDR_GET(raw, extra_length);
  uint32_t body_length = HDR_GET(raw, body_length);
  raw = NULL; // It's now safe to call non-const functions on `msg`

  // The extra section just contains the flags.
  std::string extra = msg.substr(sizeof(MsgHdr), extra_length);
  _flags = Utils::network_to_host(((uint32_t*)extra.data())[0]);
  _value = msg.substr(sizeof(MsgHdr) + extra_length + key_length, body_length - (extra_length + key_length));
}

Memcached::GetRsp::GetRsp(uint16_t status,
                          uint32_t opaque,
                          uint64_t cas,
                          const std::string& value,
                          uint32_t flags,
                          const std::string& key) :
  BaseRsp((uint8_t)OpCode::GET, "", status, opaque, cas),
  _value(value),
  _flags(flags)
{
  if (!key.empty())
  {
    // We've been passed a key to put on the response. This means we need to
    // send a GETK response rather than a GET.
    _op_code = (uint8_t)OpCode::GETK;
    _key = key;
  }
}

std::string Memcached::GetRsp::generate_extra() const
{
  std::string extras_string;

  // Only add the flags if a result has been found.
  if (_status == (uint16_t)ResultCode::NO_ERROR)
  {
    Utils::write(_flags, extras_string);
  }

  return extras_string;
}

std::string Memcached::GetRsp::generate_value() const
{
  return _value;
}

Memcached::SetAddReplaceReq::SetAddReplaceReq(const std::string& msg) :
  BaseReq(msg),
  _value(),
  _flags(0),
  _expiry(0)
{
  const char* raw = msg.data();
  uint16_t key_length = HDR_GET(raw, key_length);
  uint8_t extra_length = HDR_GET(raw, extra_length);
  uint32_t body_length = HDR_GET(raw, body_length);
  raw = NULL; // It's now safe to call non-const functions on `msg`

  std::string extra = msg.substr(sizeof(MsgHdr), extra_length);
  _flags = Utils::network_to_host(((uint32_t*)extra.data())[0]);
  _expiry = Utils::network_to_host(((uint32_t*)extra.data())[1]);
  _value = msg.substr(sizeof(MsgHdr) + (extra_length + key_length),
                      body_length - (extra_length + key_length));
}

Memcached::SetAddReplaceReq::SetAddReplaceReq(uint8_t command,
                                              std::string key,
                                              uint16_t vbucket,
                                              std::string value,
                                              uint64_t cas,
                                              uint32_t flags,
                                              uint32_t expiry) :
  BaseReq(command,
          key,
          vbucket,
          0,
          cas
         ),
  _value(value),
  _flags(flags),
  _expiry(expiry)
{
}

std::string Memcached::SetAddReplaceReq::generate_extra() const
{
  std::string ss;
  Utils::write(_flags, ss); // Flags
  Utils::write(_expiry, ss); // Expiry
  return ss;
}

std::string Memcached::SetAddReplaceReq::generate_value() const
{
  return _value;
}

Memcached::VersionRsp::VersionRsp(uint16_t status,
                                  uint32_t opaque,
                                  const std::string& version) :
  BaseRsp((uint8_t)OpCode::VERSION, "", status, opaque, 0),
  _version(version)
{
}

std::string Memcached::VersionRsp::generate_value() const
{
  return _version;
}

Memcached::TapConnectReq::TapConnectReq(const VBucketList& buckets) :
  BaseReq((uint8_t)OpCode::TAP_CONNECT,
          "",
          0,
          0,
          0
         ),
  _buckets(buckets)
{
}

std::string Memcached::TapConnectReq::generate_extra() const
{
  std::string ss;
  uint32_t extra = 0x00000002; // DUMP
  if (!_buckets.empty())
  {
    extra |= 0x00000004; // LIST_BUCKETS
  }
  Utils::write((uint32_t)extra, ss);
  return ss;
}

std::string Memcached::TapConnectReq::generate_value() const
{
  std::string ss;

  if (!_buckets.empty())
  {
    Utils::write((uint16_t)_buckets.size(), ss);
    for (VBucketIter it = _buckets.begin();
         it != _buckets.end();
         ++it)
    {
      Utils::write((uint16_t)*it, ss); // VBucket ID
    }
  }

  return ss;
}

Memcached::TapMutateReq::TapMutateReq(const std::string& msg) : BaseReq(msg)
{
  const char* raw = msg.data();
  uint16_t key_length = HDR_GET(raw, key_length);
  uint8_t extra_length = HDR_GET(raw, extra_length);
  uint32_t body_length = HDR_GET(raw, body_length);
  raw = NULL; // It's now safe to call non-const functions on `msg`

  // Byte/     0       |       1       |       2       |       3       |
  //    /              |               |               |               |
  //   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
  //   +---------------+---------------+---------------+---------------+
  //  0| Engine-specific length        | Tap flags                     |
  //   +---------------+---------------+---------------+---------------+
  //  0| TTL           | Reserved                                      |
  //   +---------------+---------------+---------------+---------------+
  //  8| Flags                                                         |
  //   +---------------+---------------+---------------+---------------+
  // 12| Expiration                                                    |
  //   +---------------+---------------+---------------+---------------+
  //   Total 8 bytes
  std::string extra = msg.substr(sizeof(MsgHdr), extra_length);
  _flags = Utils::network_to_host(((uint32_t*)extra.data())[2]);
  _expiry = Utils::network_to_host(((uint32_t*)extra.data())[3]);
  _value = msg.substr(sizeof(MsgHdr) + extra_length + key_length, body_length - (extra_length + key_length));
}

std::string Memcached::SetVBucketReq::generate_extra() const
{
  std::string ss;
  Utils::write((uint32_t)_status, ss);
  return ss;
}

Memcached::Connection::Connection() :
  _sock(-1)
{
}

Memcached::Connection::~Connection()
{
  disconnect();
}

void Memcached::Connection::disconnect()
{
  if (_sock > 0)
  {
    ::close(_sock); _sock = -1;
  }
}

bool Memcached::Connection::send(const Memcached::BaseMessage& req)
{
  if (_sock < 0)
  {
    return false;
  }

  std::string bin = req.to_wire();

  // Send the command
  if (::send(_sock, bin.data(), bin.length(), 0) < 0)
  {
    int err = errno;
    TRC_ERROR("Error during send() on socket (%d)", err);
    ::close(_sock); _sock = -1;
    return false;
  }
  return true;
}

Memcached::Status Memcached::Connection::recv(Memcached::BaseMessage** msg)
{
  if (_sock == -1)
  {
    return Memcached::Status::DISCONNECTED;
  }

  static const int BUFLEN = 16 * 1024;
  char buf[BUFLEN];
  ssize_t recv_size = 0;

  bool finished = Memcached::from_wire(_buffer, *msg);
  while (!finished)
  {
    recv_size = ::recv(_sock, buf, BUFLEN, 0);

    if (recv_size > 0)
    {
      _buffer.append(buf, recv_size);
      finished = Memcached::from_wire(_buffer, *msg);
    }
    else if (recv_size == 0)
    {
      TRC_DEBUG("Socket closed by peer");
      ::close(_sock); _sock = -1;
      return Memcached::Status::DISCONNECTED;
    }
    else
    {
      int err = errno;
      TRC_ERROR("Error during recv() on socket (%d)", err);
      ::close(_sock); _sock = -1;
      return Memcached::Status::ERROR;
    }
  }

  return Memcached::Status::OK;
}

Memcached::ClientConnection::ClientConnection(const std::string& address) :
  Connection()
{
  _address = address;
}

int Memcached::ClientConnection::connect()
{
  struct addrinfo ai_hint;
  memset(&ai_hint, 0x00, sizeof(ai_hint));
  ai_hint.ai_family = AF_UNSPEC;
  ai_hint.ai_socktype = SOCK_STREAM;

  std::string host;
  int port;
  if (!::Utils::split_host_port(_address, host, port))
  {
    return -1;
  }

  struct addrinfo* ai;
  int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &ai_hint, &ai);
  if (rc < 0)
  {
    TRC_ERROR("Failed to resolve hostname %s (%s)",
              _address.c_str(),
              gai_strerror(rc));
    return rc;
  }

  _sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (_sock < 0)
  {
    int err = errno;
    TRC_ERROR("Failed to create socket (%d)", err);
    return err;
  }

  if (::connect(_sock, ai->ai_addr, ai->ai_addrlen) < 0)
  {
    int err = errno;
    TRC_ERROR("Failed to connect to %s (%d)",
              _address.c_str(),
              err);
    ::close(_sock); _sock = -1;
    return err;
  }

  ::freeaddrinfo(ai); ai = NULL;

  return 0;
}

Memcached::ServerConnection::ServerConnection(int sock, const std::string& address) :
  Connection()
{
  _sock = sock;
  _address = address;
}

