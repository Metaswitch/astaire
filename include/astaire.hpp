#ifndef ASTAIR_H__
#define ASTAIR_H__

#include <vector>
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <boost/detail/endian.hpp>

// Simple Object Definitions
typedef uint16_t             Bucket;
typedef std::vector<Bucket>  BucketList;
typedef BucketList::const_iterator BucketIter;

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
  class Base
  {
  public:
    Base(uint8_t op_code, std::string key, uint32_t opaque, uint64_t cas) :
      _op_code(op_code),
      _key(key),
      _opaque(opaque),
      _cas(cas)
    {
    }
    Base(const std::string& msg);
    virtual ~Base() {};

    virtual const bool is_request() const = 0;
    virtual const bool is_response() const = 0;
    inline const uint8_t op_code() const { return _op_code; };
    inline const std::string& key() const { return _key; };
    inline const uint32_t opaque() const { return _opaque; };
    inline const uint64_t cas() const { return _cas; };

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

  class BaseReq : public Base
  {
  public:
    BaseReq(uint8_t command,
            std::string key,
            uint16_t vbucket,
            uint32_t opaque,
            uint64_t cas) :
      Base(command, key, opaque, cas),
      _vbucket(vbucket)
    {
    }
    BaseReq(const std::string& msg);

    const bool is_request() const { return true; }
    const bool is_response() const { return false; }
    const uint16_t vbucket() const { return _vbucket; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _vbucket; }

    uint16_t _vbucket;
  };

  class BaseRsp : public Base
  {
  public:
    BaseRsp(uint8_t command,
            std::string key,
            uint16_t status,
            uint32_t opaque,
            uint64_t cas) :
      Base(command, key, opaque, cas),
      _status(status)
    {
    }
    BaseRsp(const std::string& msg);

    const bool is_request() const { return false; }
    const bool is_response() const { return true; }
    const uint16_t result_code() const { return _status; }

  protected:
    uint16_t generate_vbucket_or_status() const { return _status; }

    uint16_t _status;
  };

  class SetReq : public BaseReq
  {
  public:
    SetReq(std::string key, uint16_t vbucket, std::string value);

  protected:
    std::string generate_extra() const;
    std::string generate_value() const;

  private:
    std::string _value;
  };

  class SetRsp : public BaseRsp
  {
  public:
    SetRsp(const std::string& msg) : BaseRsp(msg) {};
  };

  class TapConnectReq : public BaseReq
  {
  public:
    TapConnectReq();

    void setVBuckets(const std::vector<uint16_t>& buckets);

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

  private:
    std::string _value;
    uint32_t _flags;
    uint32_t _expiry;
  };

  class Connection
  {
  public:
    Connection(const std::string& address,
               const int port);
    ~Connection();

    bool connect();
    void disconnect();

    bool send(const Base& msg);
    Base* recv();

  private:
    const std::string _address;
    const int _port;
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
  bool from_wire(std::string& binary, Base*& output);

  // Parsing utility fuctions.
  bool is_msg_complete(const std::string& msg,
                       bool& request,
                       uint16_t& body_length,
                       uint8_t& op_code);
  template <class T> Base* from_wire_int(const std::string& msg)
  {
    return new T(msg);
  }
};

#endif
