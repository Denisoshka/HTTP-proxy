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
  CacheEntryChunkT *data;
  CacheEntryChunkT *lastChunk;
  volatile size_t   downloadedSize;
  volatile int      downloadFinished;
  volatile int      status;
  pthread_mutex_t   dataMutex;
  pthread_cond_t    dataCond;
};

struct CacheEntryChunk {
  size_t                  cutDataSize;
  size_t                  totalDataSize;
  struct CacheEntryChunk *next;
  char *                  data;
};

struct CacheNode {
  CacheEntryT *     entry;
  struct CacheNode *next;
  volatile int      usersQ;
};

struct CacheManager {
  double entryThreshold;

  pthread_mutex_t entriesMutex;
  CacheNodeT *    nodes;
};

void CacheNodeT_delete(const CacheNodeT *node);


void CacheEntryT_delete(CacheEntryT *entry);

void CacheEntryT_append_CacheEntryChunkT(
  CacheEntryT *entry, CacheEntryChunkT *chunk, int isLast
);


CacheManagerT *CacheManagerT_new();

CacheNodeT *CacheManagerT_acquire_CacheNodeT(
  const CacheManagerT *cache, const char *url
);

void CacheManagerT_release_CacheNodeT(
  const CacheManagerT *cache, CacheNodeT *node
);

void CacheManagerT_checkAndRemoveExpired_CacheNodeT(CacheManagerT *manager);

#undef URL_MAX_LENGTH
#endif
