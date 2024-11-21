#ifndef PROXY_H
#define PROXY_H

#include <pthread.h>

#include "../cache/cache.h"
#define CACHE_SIZE_LIMIT 1048576 //1Mb
//Should be >= 16kB to fully fit HTTP headers in single buffer
#define BUFFER_SIZE      16384  //16kB 


static const size_t kDefaultChunkSize = (1024 * 1024);

typedef struct ClientContextArgs {
  CacheManagerT *cacheManager;
  int            clientSocket;
} ClientContextArgsT;


//request_handler.c funcs
void handleRequest(int clientSocket);

void *downloadData(void *args);

//server.c funcs
void startServer(int port);

#endif //PROXY_H
