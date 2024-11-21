#ifndef PROXY_H
#define PROXY_H

#include <pthread.h>
#include <stdio.h>

#include "../cache/cache.h"

#define CACHE_SIZE_LIMIT 1048576 //1Mb

/**
 *Should be >= 16kB to fully fit HTTP headers in single buffer
 */
#define BUFFER_SIZE      16384  //16kB 


static const size_t kDefaultChunkSize = (1024 * 1024);

typedef struct ClientContextArgs {
  CacheManagerT *cacheManager;
  int            clientSocket;
} ClientContextArgsT;

ssize_t readHttpHeaders(int client_socket, char *buffer, size_t buffer_size);

int parseURL(const char *url, char *host, char *path, int *port);

void handleRequest(int clientSocket);

void *downloadData(void *args);

void startServer(int port);

#endif //PROXY_H
