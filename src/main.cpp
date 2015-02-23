#include "astaire.hpp"
#include "memcachedstore.h"

#include <iostream>
#include <errno.h>
#include <cassert>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "Usage: ./astaire <hostname>\n");
    return 3;
  }

  Memcached::Connection cxn(argv[1], 11211);
  if (!cxn.connect())
  {
    return 2;
  }
  Memcached::Connection cxn2(argv[1], 11212);
  if (!cxn2.connect())
  {
    return 2;
  }

  /* Uncomment these lines if vBucket-policing is enabled on the server.  This
   * is controlled by the `-e ignore_vbucket=true` line in
   * /etc/memcached_11211.conf` except that doesn't work....
   */
  printf("Enabling vBuckets\n");
  for (uint16_t ii = 0; ii < 128; ii++)
  {
    Memcached::SetVBucketReq setvb(ii, Memcached::VBucketStatus::ACTIVE);
    cxn.send(setvb);
    Memcached::BaseRsp* rsp = (Memcached::BaseRsp*)cxn.recv();
    if (rsp->result_code() != 0)
    {
      fprintf(stderr,
              "Failed to activate vbucket %d, error: %d\n",
              ii,
              rsp->result_code());
      return 2;
    }
  }
  for (uint16_t ii = 0; ii < 128; ii++)
  {
    Memcached::SetVBucketReq setvb(ii, Memcached::VBucketStatus::ACTIVE);
    cxn2.send(setvb);
    Memcached::BaseRsp* rsp = (Memcached::BaseRsp*)cxn2.recv();
    if (rsp->result_code() != 0)
    {
      fprintf(stderr,
              "Failed to activate vbucket %d, error: %d\n",
              ii,
              rsp->result_code());
      return 2;
    }
  }
  /**/

  printf("Enabled vBuckets\n");
  printf("Press enter to continue...\n");
  getchar();
  printf("Inserting keys\n");

  Log::setLoggingLevel(1);
  Store* store = new MemcachedStore(true,
                                    "./servers.conf");
  Store* store2 = new MemcachedStore(true,
                                     "./servers2.conf");

  for (int ii = 0; ii < 100; ii++)
  {
    std::string key = "key" + std::to_string(ii);
    Store::Status rc = store->set_data("table",
                                       key,
                                       "value",
                                       0,
                                       5000);
    if (rc != Store::OK)
    {
      fprintf(stderr, "Failed to add key: %d\n", rc);
      return 2;
    }
  }

  printf("Inserted key0-key99\n");
  printf("Press enter to continue...\n");
  getchar();
  printf("Starting TAP\n");

  // TODO Calculate source servers and bucket lists.

  Memcached::TapConnectReq tap({1, 4, 6});
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
//      std::cout << "Received TAP_MUTATE("
//                << (uint32_t)mutate->op_code()
//                << ") for key: "
//                << mutate->key()
//                << " and value: "
//                << mutate->value()
//                << std::endl;

      // Split table from key.
      std::string full_key = mutate->key();
      size_t split_pos = full_key.find("//");
      std::string table = full_key.substr(0, split_pos);
      std::string key = full_key.substr(split_pos + 2, std::string::npos);

      Store::Status rc = store2->set_data(table,
                                          key,
                                          mutate->value(),
                                          0,
                                          5000);
      if (rc != Store::OK)
      {
        fprintf(stderr, "Failed to add tapped key: %d\n", rc);
      }
    }

    free(req);
  }
  while (!finished);

  printf("Finished TAP\n");
  printf("Press enter to continue...\n");
  getchar();

  return 0;
}
