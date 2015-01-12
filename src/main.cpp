#include "astaire.hpp"

#include <iostream>
#include <errno.h>
#include <cassert>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef int socket_t;

bool send_req(const Memcached::BaseReq& req, socket_t sock)
{
  std::string bin = req.to_wire();

  // Send the command
  if (send(sock, bin.data(), bin.length(), 0) < 0)
  {
    close(sock);
    perror("send():");
    return false;
  }
  return true;
}

Memcached::BaseRsp* recv_rsp(socket_t sock)
{
  // Read the response
#define BUFLEN 128
  char buf[BUFLEN];
  Memcached::Base* msg = NULL;
  ssize_t recv_size = 0;
  static std::string raw;
  bool finished = Memcached::from_wire(raw, msg);
  while (!finished)
  {
    recv_size = recv(sock, buf, BUFLEN, 0);

    if (recv_size > 0)
    {
      raw.append(buf, recv_size);
      finished = Memcached::from_wire(raw, msg);
    }
    else if (recv_size == 0)
    {
      fprintf(stderr, "Socket closed by peer\n");
      close(sock);
      return NULL;
    }
    else
    {
      perror("recv():");
      close(sock);
      return NULL;
    }
  }

  return (Memcached::BaseRsp*)msg;
}

int main()
{
  struct addrinfo ai_hint;
  memset(&ai_hint, 0x00, sizeof(ai_hint));
  ai_hint.ai_family = AF_INET;
  ai_hint.ai_socktype = SOCK_STREAM;

  struct addrinfo* ai;
  int rc = getaddrinfo("127.0.0.1", "11211", &ai_hint, &ai);
  if (rc < 0)
  {
    fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(rc));
    return 2;
  }

  socket_t sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
  if (sock < 0)
  {
    perror("socket():");
    return 2;
  }

  if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0)
  {
    close(sock);
    perror("connect():");
    return 2;
  }

  freeaddrinfo(ai); ai = NULL;

  for (int ii = 0; ii < 10; ii++)
  {
    std::string key = "key" + std::to_string(ii);
    Memcached::SetReq set(key, 0, "value");
    send_req(set, sock);
    Memcached::SetRsp* rsp = (Memcached::SetRsp*)recv_rsp(sock);
    assert(rsp->result_code() == 0);
  }

  Memcached::TapConnectReq tap;
  send_req(tap, sock);

  bool finished = false;
  do
  {
    Memcached::BaseRsp* req = recv_rsp(sock);
    if (req == NULL)
    {
      finished = true;
      continue;
    }

    if (req->op_code() == 0x41) // TAP_MUTATE
    {
      Memcached::TapMutateReq* mutate = (Memcached::TapMutateReq*)req;
      std::cout << "Received TAP_MUTATE(" << (uint32_t)mutate->op_code() << ") for key: " << mutate->key() << " and value: " << mutate->value() << std::endl;
    }
  }
  while (!finished);

  return 0;
}
