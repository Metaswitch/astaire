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

int main()
{
  Memcached::Connection cxn("localhost", 11211);
  if (!cxn.connect())
  {
    return 2;
  }

  for (int ii = 0; ii < 10; ii++)
  {
    std::string key = "key" + std::to_string(ii);
    Memcached::SetReq set(key, 0, "value");
    cxn.send(set);
    Memcached::SetRsp* rsp = (Memcached::SetRsp*)cxn.recv();
    assert(rsp->result_code() == 0);
  }

  Memcached::TapConnectReq tap;
  cxn.send(tap);

  bool finished = false;
  do
  {
    Memcached::BaseRsp* req = (Memcached::BaseRsp*)cxn.recv();
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

    free(req);
  }
  while (!finished);

  return 0;
}
