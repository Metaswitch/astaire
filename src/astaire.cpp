#include "astaire.hpp"

#include <cstring>
#include <cassert>

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

Memcached::TapConnectReq::TapConnectReq() :
  BaseReq(0x40, // TAP_CONNECT
          "",
          0,
          0,
          0
         )
{
}

std::string Memcached::TapConnectReq::generate_extra() const
{
  std::string ss;
  uint64_t extra = 0x0000000000000002; // DUMP
  if (!_buckets.empty())
  {
    extra |= 0x0000000000000004; // LIST_BUCKETS
  }
  Utils::write((uint64_t)extra, ss);
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

inline void Memcached::TapConnectReq::setVBuckets(const BucketList& buckets)
{
  _buckets = buckets;
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
