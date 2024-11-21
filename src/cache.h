#ifndef CACHE_H
#define CACHE_H

#define URL_MAX_LENGTH   2048
#include <pthread.h>
#include <stddef.h>

typedef struct CacheEntry      CacheEntryT;
typedef struct CacheNode       CacheNodeT;
typedef struct Cache           CacheT;
typedef struct CacheEntryChunk CacheEntryChunkT;

struct CacheEntry {
  char              url[URL_MAX_LENGTH];
  CacheEntryChunkT *data;
  CacheEntryChunkT *lastChunk;
  volatile size_t   downloadedSize;
  volatile int      downloadFinished;
  pthread_mutex_t   dataMutex;
  pthread_cond_t    dataAvailable;
};

struct CacheEntryChunk {
  char *                data;
  size_t                chunkSize;
  volatile CacheEntryT *next = NULL;
};

struct CacheNode {
  CacheEntryT *     entry;
  volatile int      usersQ;
  struct CacheNode *next;
};

struct Cache {
  pthread_mutex_t entriesMutex;
  CacheNodeT *    entries;
  size_t          curCacheSize;
  size_t          cacheSizeLimit;
};

#undef URL_MAX_LENGTH
#endif //CACHE_H
