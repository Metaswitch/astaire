#include "astaire.hpp"

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
    // Too short afterall
    return false;
  }

  // This is a complete message, get the op-code out too.
  op_code = HDR_GET(raw, op_code);

  return true;
}

bool Memcached::from_wire(std::string& msg,
                          Memcached::Base*& output)
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
    case 0x41:
      // TAP_MUTATE
      output = from_wire_int<Memcached::TapMutateReq>(msg);
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
    case 0x01:
      // SET
      output = Memcached::from_wire_int<Memcached::SetRsp>(msg);
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

std::string Memcached::Base::to_wire() const
{
  std::string ss;

  // Build the message-specific sections.
  std::string extra = generate_extra();
  std::string value = generate_value();
  uint16_t vbucket_or_status = generate_vbucket_or_status();

  // Calculate body size, this is the sum of the sizes of Extras, Key and
  // Values sections.
  uint32_t body_size = extra.length() + _key.length() + value.length();

  Utils::write((uint8_t)0x80, ss); // Magic byte (0x80 - REQUEST)
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

Memcached::Base::Base(const std::string& msg)
{
  const char* raw = msg.data();
  _op_code = HDR_GET(raw, op_code);
  _opaque = HDR_GET(raw, opaque);
  _cas = HDR_GET(raw, cas);
  _key = msg.substr(sizeof(MsgHdr) + HDR_GET(raw, extra_length),
                    HDR_GET(raw, key_length));
}

Memcached::BaseReq::BaseReq(const std::string& msg) : Base(msg)
{
  _vbucket = HDR_GET(msg.data(), vbucket_or_status);
}

Memcached::BaseRsp::BaseRsp(const std::string& msg) : Base(msg)
{
  _status = HDR_GET(msg.data(), vbucket_or_status);
}

Memcached::SetReq::SetReq(std::string key,
                          uint16_t vbucket,
                          std::string value) :
  BaseReq(0x01, // SET
          key,
          vbucket,
          0,
          0
         ),
  _value(value)
{
}

std::string Memcached::SetReq::generate_extra() const
{
  std::string ss;
  Utils::write((uint32_t)0x00000000, ss); // Flags
  Utils::write((uint32_t)0x00000000, ss); // Expiry
  return ss;
}

std::string Memcached::SetReq::generate_value() const
{
  return _value;
}

Memcached::TapConnectReq::TapConnectReq(const BucketList& buckets) :
  BaseReq(0x40, // TAP_CONNECT
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
    for (BucketIter it = _buckets.begin();
         it != _buckets.end();
         ++it)
    {
      Utils::write((uint16_t)*it, ss); // Bucket ID
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

  // The extra section is the same as for a SET command:
  //
  // Byte/     0       |       1       |       2       |       3       |
  //    /              |               |               |               |
  //   |0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|0 1 2 3 4 5 6 7|
  //   +---------------+---------------+---------------+---------------+
  //  0| Flags                                                         |
  //   +---------------+---------------+---------------+---------------+
  //  4| Expiration                                                    |
  //   +---------------+---------------+---------------+---------------+
  //   Total 8 bytes
  std::string extra = msg.substr(sizeof(MsgHdr), extra_length);
  _flags = Utils::network_to_host(((uint32_t*)extra.data())[0]);
  _expiry = Utils::network_to_host(((uint32_t*)extra.data())[1]);
  _value = msg.substr(sizeof(MsgHdr) + extra_length + key_length, body_length - (extra_length + key_length));
}

std::string Memcached::SetVBucketReq::generate_extra() const
{
  std::string ss;
  Utils::write((uint32_t)_status, ss);
  return ss;
}

Memcached::Connection::Connection(const std::string& address,
                                  int port) :
  _address(address),
  _port(port),
  _sock(-1)
{
}

Memcached::Connection::~Connection()
{
  disconnect();
}

bool Memcached::Connection::connect()
{
  struct addrinfo ai_hint;
  memset(&ai_hint, 0x00, sizeof(ai_hint));
  ai_hint.ai_family = AF_INET;
  ai_hint.ai_socktype = SOCK_STREAM;

  struct addrinfo* ai;
  int rc = getaddrinfo(_address.c_str(), std::to_string(_port).c_str(), &ai_hint, &ai);
  if (rc < 0)
  {
    fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(rc));
    return false;
  }

  _sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (_sock < 0)
  {
    perror("socket()");
    return false;
  }

  if (::connect(_sock, ai->ai_addr, ai->ai_addrlen) < 0)
  {
    close(_sock); _sock = -1;
    perror("connect()");
    return false;
  }

  freeaddrinfo(ai); ai = NULL;

  return true;
}

void Memcached::Connection::disconnect()
{
  if (_sock > 0)
  {
    close(_sock); _sock = -1;
  }
}

bool Memcached::Connection::send(const Memcached::Base& req)
{
  if (_sock == -1)
  {
    return false;
  }

  std::string bin = req.to_wire();

  // Send the command
  if (::send(_sock, bin.data(), bin.length(), 0) < 0)
  {
    close(_sock); _sock = -1;
    perror("send():");
    return false;
  }
  return true;
}

Memcached::Base* Memcached::Connection::recv()
{
  if (_sock == -1)
  {
    return NULL;
  }

#define BUFLEN 128
  char buf[BUFLEN];
  Memcached::Base* msg = NULL;
  ssize_t recv_size = 0;

  bool finished = Memcached::from_wire(_buffer, msg);
  while (!finished)
  {
    recv_size = ::recv(_sock, buf, BUFLEN, 0);

    if (recv_size > 0)
    {
      _buffer.append(buf, recv_size);
      finished = Memcached::from_wire(_buffer, msg);
    }
    else if (recv_size == 0)
    {
      fprintf(stderr, "Socket closed by peer\n");
      close(_sock); _sock = -1;
      return NULL;
    }
    else
    {
      perror("recv():");
      close(_sock); _sock = -1;
      return NULL;
    }
  }

  return (Memcached::BaseRsp*)msg;
}
