#ifndef CACHE_H
#define CACHE_H

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

typedef enum CacheStatus {
  InProcess,
  Success,
  Failed,
}CacheStatusT;

struct CacheEntry {
  char *                     url;
  struct timeval             lastUpdate;
  volatile CacheEntryChunkT *dataChunks;
  volatile CacheEntryChunkT *lastChunk;
  volatile size_t            downloadedSize;
  volatile enum CacheStatus  status;
  volatile int               usersQ;
  pthread_mutex_t            dataMutex;
  pthread_cond_t             dataCond;
};

struct CacheEntryChunk {
  size_t                  curDataSize;
  size_t                  maxDataSize;
  struct CacheEntryChunk *next;
  char *                  data;
};

struct CacheNode {
  CacheEntryT *     entry;
  struct CacheNode *next;
};

struct CacheManager {
  double entryThreshold;

  pthread_mutex_t entriesMutex;
  CacheNodeT *    nodes;
  CacheNodeT *    lastNode;
};


CacheNodeT *CacheNodeT_new();

void CacheNodeT_delete(CacheNodeT *node);

CacheEntryT *CacheEntryT_new();

void CacheEntryT_acquire(CacheEntryT *entry);

void CacheEntryT_delete(CacheEntryT *entry);

void CacheEntryT_release(CacheEntryT *entry);

void CacheEntryT_updateStatus(CacheEntryT *         entry,
                              CacheStatusT status);

void CacheEntryChunkT_delete(CacheEntryChunkT *chunk);

CacheEntryChunkT *CacheEntryChunkT_new(size_t dataSize);

void CacheEntryT_append_CacheEntryChunkT(
  CacheEntryT *entry, CacheEntryChunkT *chunk
);


CacheManagerT *CacheManagerT_new();

void CacheManagerT_put_CacheNodeT(CacheManagerT *cache, CacheNodeT *node);

CacheNodeT *CacheManagerT_get_CacheNodeT(
  const CacheManagerT *cache, const char *url
);

CacheEntryT *CacheEntryT_new_withUrl(const char *url);

void CacheManagerT_checkAndRemoveExpired_CacheNodeT(CacheManagerT *manager);

#undef URL_MAX_LENGTH
#endif
