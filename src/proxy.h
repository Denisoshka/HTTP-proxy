#ifndef PROXY_H
#define PROXY_H

#include <pthread.h>
#define CACHE_SIZE_LIMIT 1048576 //1Mb
//Should be >= 16kB to fully fit HTTP headers in single buffer
#define BUFFER_SIZE      16384  //16kB 


static const size_t kDefaultChunkSize = (1024 * 1024);

enum CacheStatus {
  Success,
  NotFound,
  Error,
};


//cache.c funcs
CacheEntryT *getOrCreateCacheEntry(const char *url);

void deleteEntry(CacheEntryT *entry);

void cacheCleanup(void);

void cacheInsertData(CacheEntryT *entry, const char *data, size_t length);

void cacheMarkComplete(CacheEntryT *entry);

void cacheMarkOk(CacheEntryT *entry, int val);

//request_handler.c funcs
void handleRequest(int clientSocket);

void *downloadData(void *args);

//server.c funcs
void startServer(int port);

#endif //PROXY_H
