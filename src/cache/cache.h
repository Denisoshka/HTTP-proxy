#ifndef CACHE_H
#define CACHE_H

#define URL_MAX_LENGTH   2048
#include <pthread.h>

#define CHECK_RET(ret, action)                                \
  do {                                                        \
    if ((ret) != 0) {                                         \
      logFatal("[Error] %s : %s", action, strerror(ret));     \
      abort();                                                \
    }                                                         \
  } while (0)

typedef struct CacheEntry      CacheEntryT;
typedef struct CacheNode       CacheNodeT;
typedef struct CacheManager    CacheManagerT;
typedef struct CacheEntryChunk CacheEntryChunkT;

enum CacheEvents {
  RequireToCleanCash
};

struct CacheEntry {
  char              url[URL_MAX_LENGTH];
  struct timeval    lastUpdate;
  size_t            Threshold;
  CacheEntryChunkT *data;
  CacheEntryChunkT *lastChunk;
  volatile size_t   downloadedSize;
  volatile int      downloadFinished;
  pthread_mutex_t   dataMutex;
  pthread_cond_t    dataCond;
};

struct CacheEntryChunk {
  size_t       chunkSize;
  CacheEntryT *next;
  char *       data;
};

struct CacheNode {
  CacheEntryT *     entry;
  struct CacheNode *next;
  volatile int      usersQ;
};

struct CacheManager {
  size_t curCacheSize;
  size_t cacheSizeLimit;

  pthread_mutex_t entriesMutex;
  CacheNodeT *    nodes;
};

CacheNodeT *CacheManagerT_acquire_CacheNodeT(
  const CacheManagerT *cache, const char *url
);

void CacheNodeT_delete(CacheNodeT *node);

#undef URL_MAX_LENGTH
#endif
