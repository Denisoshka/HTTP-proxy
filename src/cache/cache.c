#include "cache.h"
#include "../log.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <bits/pthreadtypes.h>
#include <sys/time.h>

#define SUCCESS 0
#define FAILURE -1

static bool isCacheNodeValid(CacheEntryT *entry, const struct timeval now) {
  const long elapsedMs = (now.tv_sec - entry->lastUpdate.tv_sec) * 1000
                         + (now.tv_usec - entry->lastUpdate.tv_usec) / 1000;
  entry->lastUpdate.tv_sec = now.tv_sec;
  return elapsedMs <= entry->entryThreshold;
}

CacheManagerT *CacheManagerT_new(const size_t cacheSizeLimit) {
  CacheManagerT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }

  tmp->cacheSizeLimit = cacheSizeLimit;
  tmp->nodes = NULL;
  pthread_mutexattr_t attr;

  int ret = pthread_mutexattr_init(&attr);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutexattr_init failed %s", strerror(errno)
    );
    goto destroyAtMalloc;
  }

  ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutexattr_settype failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  ret = pthread_mutex_init(&tmp->entriesMutex, &attr);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutex_init failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  return tmp;
destroyAtMutexAtrs:
  pthread_mutexattr_destroy(&attr);
destroyAtMalloc:
  free(tmp);
  tmp = NULL;
  return tmp;
}

CacheNodeT *CacheManagerT_acquire_CacheNodeT(const CacheManagerT *cache,
                                             const char *         url) {
  for (
    CacheNodeT *node = cache->nodes;
    node != NULL;
    node = node->next
  ) {
    if (strcmp(node->entry->url, url) == 0) {
      int ret = pthread_mutex_lock(&node->entry->dataMutex);
      if (ret != 0) {
        logFatal(
          "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_init failed %s",
          strerror(errno)
        );
        abort();
      }
      node->usersQ++;
      node->entry->lastUpdate = time(NULL);
      ret = pthread_mutex_unlock(&node->entry->dataMutex);
      if (ret != 0) {
        logFatal(
          "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_init failed %s",
          strerror(errno)
        );
        abort();
      }
      return node;
    }
  }
  return NULL;
}

void CacheManagerT_release_CacheNodeT(
  const CacheManagerT *cache,
  CacheNodeT *         node
) {
  int ret = pthread_mutex_lock(&node->entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_lock failed %s",
      strerror(errno)
    );
    abort();
  }

  node->usersQ--;
  node->entry->lastUpdate = time(NULL);

  ret = pthread_mutex_unlock(&node->entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_unlock failed %s",
      strerror(errno)
    );
    abort();
  }
}

CacheEntryT *CacheEntryT_register_CacheEntryChunkT(
  CacheEntryT *     entry,
  CacheEntryChunkT *chunk
) {
  int ret = pthread_mutex_lock(&entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheEntryT_insertNew_CacheEntryChunkT] pthread_mutex_lock : %s",
      strerror(errno)
    );
    abort();
  }

  if (entry->data == NULL) {
    entry->lastChunk = chunk;
    entry->data = chunk;
  }

  entry->lastChunk->next = entry;
  entry->downloadedSize += chunk->chunkSize;
  gettimeofday(&entry->lastUpdate, NULL);

  ret = pthread_cond_broadcast(&entry->dataCond);
  if (ret != 0) {
    logFatal(
      "[CacheEntryT_insertNew_CacheEntryChunkT] pthread_mutex_lock : %s",
      strerror(errno)
    );
    abort();
  }

  ret = pthread_mutex_unlock(&entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheEntryT_insertNew_CacheEntryChunkT] pthread_mutex_lock : %s",
      strerror(errno)
    );
    abort();
  }

  return NULL;
}

void CacheManagerT_checkAndRemoveExpired_CacheNodeT(CacheManagerT *manager) {
  pthread_mutex_lock(&manager->entriesMutex);
  struct timeval checkStart;
  gettimeofday(&checkStart, NULL);

  for (CacheNodeT **current = &manager->nodes; (*current) != NULL;) {
    CacheNodeT *node = *current;
    int         ret = pthread_mutex_lock(&node->entry->dataMutex);
    if (ret != 0) {
      logFatal(
        "[CacheManagerT_checkAndRemoveExpired_CacheNodeT] pthread_mutex_lock : %s",
        strerror(errno)
      );
      abort();
    }
    if (!isCacheNodeValid(node->entry, checkStart) && node->usersQ == 0) {
      *current = node->next;
      CacheNodeT_delete()
    } else {
      current = &node->next;
    }
    ret = pthread_mutex_unlock(&node->entry->dataMutex);
    if (ret != 0) {
      logFatal(
        "[CacheManagerT_checkAndRemoveExpired_CacheNodeT] pthread_mutex_unlock : %s",
        strerror(errno)
      );
      abort();
    }
  }
}

void removeOldestEntry(void) {
  CacheEntryT *entry = cache;
  CacheEntryT *prev = NULL;
  CacheEntryT *oldestEntry = cache;
  CacheEntryT *oldestPrev = NULL;
  //todo
  while (entry) {
    if (entry->lastAccessTime < oldestEntry->lastAccessTime) {
      oldestEntry = entry;
      oldestPrev = prev;
    }
    prev = entry;
    entry = entry->next;
  }

  if (oldestPrev) {
    oldestPrev->next = oldestEntry->next;
  } else {
    cache = oldestEntry->next;
  }

  free(oldestEntry->data);
  pthread_mutex_destroy(&oldestEntry->dataMutex);
  pthread_cond_destroy(&oldestEntry->dataAvailable);
  free(oldestEntry);
}

CacheEntryT *putCacheEntry(CacheManagerT *cache, CacheEntryT *entry) {
}

void cacheInsertData(CacheEntryT *entry, const char *data,
                     const size_t length) {
  pthread_mutex_lock(&entry->dataMutex);

  entry->data = realloc(entry->data, entry->downloadedSize + length);
  memcpy(entry->data + entry->downloadedSize, data, length);
  entry->downloadedSize += length;

  pthread_cond_broadcast(&entry->dataAvailable);
  pthread_mutex_unlock(&entry->dataMutex);
}

void cacheMarkOk(CacheEntryT *entry, const int val) {
  pthread_mutex_lock(&entry->dataMutex);
  entry->isOk = val;
  pthread_mutex_unlock(&entry->dataMutex);
}

void cacheMarkComplete(CacheEntryT *entry) {
  pthread_mutex_lock(&entry->dataMutex);
  entry->dataAvalible = 1;
  pthread_cond_broadcast(&entry->dataAvailable);
  pthread_mutex_unlock(&entry->dataMutex);
}

void cacheCleanup(void) {
  pthread_mutex_lock(&cacheMutex);
  CacheEntryT *cur = cache;
  CacheEntryT *prev = NULL;

  while (cur) {
    if (cur->dataAvalible && cur->downloadedSize > CACHE_SIZE_LIMIT / 2) {
      if (prev) {
        prev->next = cur->next;
      } else {
        cache = cur->next;
      }

      free(cur->data);
      pthread_mutex_destroy(&cur->dataMutex);
      pthread_cond_destroy(&cur->dataAvailable);
      free(cur);
      cur = (prev) ? prev->next : cache;
    } else {
      prev = cur;
      cur = cur->next;
    }
  }

  pthread_mutex_unlock(&cacheMutex);
}
