/**
 * @file memcached_tap_client.hpp
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

#ifndef MEMCACHED_TAP_CLIENT_H__
#define MEMCACHED_TAP_CLIENT_H__

#include <vector>
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <boost/detail/endian.hpp>
#include <log.h>

// Simple Object Definitions
typedef uint16_t             VBucket;
typedef std::vector<VBucket>  VBucketList;
typedef VBucketList::const_iterator VBucketIter;

#define HDR_GET(RAW, FIELD) \
  ::Memcached::Utils::network_to_host(((MsgHdr*)(RAW))->FIELD)

// Namespace for memcached operations
namespace Memcached
{
  namespace Utils
  {
    inline uint8_t host_to_network(uint8_t v) {return v;}
    inline uint8_t network_to_host(uint8_t v) {return v;}
    inline uint16_t host_to_network(uint16_t v) {return htons(v);}
    inline uint16_t network_to_host(uint16_t v) {return ntohs(v);}
    inline uint32_t host_to_network(uint32_t v) {return htonl(v);}
    inline uint32_t network_to_host(uint32_t v) {return ntohl(v);}
    inline uint64_t host_to_network(uint64_t v)
    {
#ifdef BOOST_LITTLE_ENDIAN
      uint64_t low = htonl(static_cast<uint32_t>(v >> 32));
      uint64_t high = htonl(static_cast<uint32_t>(v & 0xFFFFFFFF));
      return (low | (high << 32));
#else
      return v;
#endif
    }
    inline uint64_t network_to_host(uint64_t v) { return host_to_network(v); }

    void write(const std::string& value, std::string& str);
    template<class T> void write(const T& value, std::string& str)
    {
      T network_value = host_to_network(value);
      str.append(reinterpret_cast<const char*>(&network_value), sizeof(T));
    }
  }

  enum struct OpCode
  {
    GET = 0x00,
    SET = 0x01,
    ADD = 0x02,
    REPLACE = 0x03,
    TAP_CONNECT = 0x40,
    TAP_MUTATE = 0x41,
    SET_VBUCKET = 0x3d
  };

  enum struct ResultCode
  {
    NO_ERROR = 0x0000,
    KEY_NOT_FOUND = 0x0001,
    KEY_EXISTS = 0x0002,
    VALUE_TOO_LARGE = 0x0003,
    INVALID_ARGUMENTS = 0X0004,
    ITEM_NOT_STORED = 0X0005,
    INCR_DECR_ON_NON_NUMERIC_VALUE = 0X0006,
    UNKNOWN_COMMAND = 0X0081,
    OUT_OF_MEMORY = 0X0082
  };

  enum struct VBucketStatus
  {
    ACTIVE = 0x01,
    REPLICA = 0x02,
    PENDING = 0x03,
    DEAD = 0x04
  };

  enum struct Status
  {
    OK,
    DISCONNECTED,
    ERROR,
  };

  /* Binary structure of the fixed-length header for Memcached messages */
  struct MsgHdr
  {
    uint8_t magic;
    uint8_t op_code;
    uint16_t key_length;
    uint8_t extra_length;
    uint8_t data_type;
    uint16_t vbucket_or_status;
    uint32_t body_length;
    uint32_t opaque;
    uint64_t cas;
  };

  /* This abstract base class represents a generic Memcached message.
   *
   * This class is mostly used for defining common utilities and specifying a
   * common API. */
  class BaseMessage
  {
  public:
    BaseMessage(uint8_t op_code, std::string key, uint32_t opaque, uint64_t cas) :
      _op_code(op_code),
      _key(key),
      _opaque(opaque),
      _cas(cas)
    {
    }
    BaseMessage(const std::string& msg);
    virtual ~BaseMessage() {};

    virtual bool is_request() const = 0;
    virtual bool is_response() const = 0;
    inline uint8_t op_code() const { return _op_code; };
    inline const std::string& key() const { return _key; };
    inline uint32_t opaque() const { return _opaque; };
    inline uint64_t cas() const { return _cas; };

    std::string to_wire() const;

  protected:
    virtual std::string generate_extra() const{ return ""; };
    virtual std::string generate_value() const { return ""; };
    virtual uint16_t generate_vbucket_or_status() const = 0;

  private:
    uint8_t _op_code;
    std::string _key;
    uint32_t _opaque;
    uint64_t _cas;
  };

  class BaseReq : public BaseMessage
  {
  public:
    BaseReq(uint8_t command,
            std::string key,
            uint16_t vbucket,
            uint32_t opaque,
            uint64_t cas) :
      BaseMessage(command, key, opaque, cas),
      _vbucket(vbucket)
    {
    }
    BaseReq(const std::string& msg);

    bool is_request() const { return true; }
    bool is_response() const { return false; }
    uint16_t vbucket() const { return _vbucket; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _vbucket; }

    uint16_t _vbucket;
  };

  class BaseRsp : public BaseMessage
  {
  public:
    BaseRsp(uint8_t command,
            std::string key,
            uint16_t status,
            uint32_t opaque,
            uint64_t cas) :
      BaseMessage(command, key, opaque, cas),
      _status(status)
    {
    }
    BaseRsp(const std::string& msg);

    bool is_request() const { return false; }
    bool is_response() const { return true; }
    uint16_t result_code() const { return _status; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _status; }

    uint16_t _status;
  };

  class GetReq : public BaseReq
  {
  public:
    GetReq(std::string key) : BaseReq((uint8_t)OpCode::GET, key, 0, 0, 0) {}
  };

  class GetRsp : public BaseRsp
  {
  public:
    GetRsp(const std::string& msg);

    std::string value() const { return _value; };
    uint32_t flags() const { return _flags; };

  private:
    std::string _value;
    uint32_t _flags;
  };

  class SetAddReplaceReq : public BaseReq
  {
  public:
    SetAddReplaceReq(uint8_t command,
                     std::string key,
                     uint16_t vbucket,
                     std::string value,
                     uint64_t cas,
                     uint32_t flags,
                     uint32_t expiry);

  protected:
    std::string generate_extra() const;
    std::string generate_value() const;

  private:
    std::string _value;
    uint32_t _flags;
    uint32_t _expiry;
  };

  class SetAddReplaceRsp : public BaseRsp
  {
  public:
    SetAddReplaceRsp(const std::string& msg) : BaseRsp(msg) {};
  };

  class SetReq : public SetAddReplaceReq
  {
  public:
    SetReq(std::string key,
           uint16_t vbucket,
           std::string value,
           uint32_t flags,
           uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::SET, key, vbucket, value, 0, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp SetRsp;

  class AddReq : public SetAddReplaceReq
  {
  public:
    AddReq(std::string key,
           uint16_t vbucket,
           std::string value,
           uint32_t flags,
           uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::ADD, key, vbucket, value, 0, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp AddRsp;

  class ReplaceReq : public SetAddReplaceReq
  {
  public:
    ReplaceReq(std::string key,
               uint16_t vbucket,
               std::string value,
               uint64_t cas,
               uint32_t flags,
               uint32_t expiry) :
      SetAddReplaceReq((uint8_t)OpCode::SET, key, vbucket, value, cas, flags, expiry)
    {}
  };

  typedef SetAddReplaceRsp ReplaceRsp;

  class TapConnectReq : public BaseReq
  {
  public:
    TapConnectReq(const VBucketList& buckets);

  protected:
    std::string generate_extra() const;
    std::string generate_value() const;

  private:
    std::vector<uint16_t> _buckets;
  };

  class TapMutateReq : public BaseReq
  {
  public:
    TapMutateReq(const std::string& msg);

    std::string value() const { return _value; };
    uint32_t flags() const { return _flags; };
    uint32_t expiry() const { return _expiry; };

  private:
    std::string _value;
    uint32_t _flags;
    uint32_t _expiry;
  };

  class SetVBucketReq : public BaseReq
  {
  public:
    SetVBucketReq(uint16_t vbucket, VBucketStatus status) :
      BaseReq((uint8_t)OpCode::SET_VBUCKET, // Command Code
              "",                           // Key
              vbucket,                      // VBucket
              0,                            // Opaque (unused)
              0),                           // CAS (unused)
      _status(status)
    {
    }

    std::string generate_extra() const;

  private:
    VBucketStatus _status;
  };

  class Connection
  {
  public:
    Connection(const std::string& address);
    ~Connection();

    int connect();
    void disconnect();

    bool send(const BaseMessage& msg);
    Status recv(BaseMessage** msg);

  private:
    const std::string _address;
    int _sock;
    std::string _buffer;
  };

  // Entry point for parsing messages off the wire.
  //
  // @returns  True if the string contains a complete message.
  // @param binary - The message off the wire.  If the message is
  //                 complete, it is removed from the front of the string
  //                 before this function returns.
  // @param output - A pointer to store the parsed message.
  bool from_wire(std::string& binary, BaseMessage*& output);

  // Parsing utility fuctions.
  bool is_msg_complete(const std::string& msg,
                       bool& request,
                       uint16_t& body_length,
                       uint8_t& op_code);
  template <class T> BaseMessage* from_wire_int(const std::string& msg)
  {
    return new T(msg);
  }
};

#endif
