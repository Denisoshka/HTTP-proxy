#include "cache.h"


#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bits/pthreadtypes.h>

#include "log.h"

CacheT *CacheEntryT_new(const size_t cacheSizeLimit) {
  CacheT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->cacheSizeLimit = cacheSizeLimit;
  tmp->entries = NULL;
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_init failed %s", strerror(errno)
    );
    goto destroyAtMalloc;
  }
  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_settype failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  if (pthread_mutex_init(&tmp->entriesMutex, &attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutex_init failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  tmp->cacheSizeLimit = cacheSizeLimit;
  tmp->curCacheSize = 0;
  return tmp;

destroyAtMutexAtrs:
  pthread_mutexattr_destroy(&attr);
destroyAtMalloc:
  free(tmp);
  tmp = NULL;
  return tmp;
}

CacheEntryChunkT *CacheEntryChunk_new(const size_t dataSize) {
  CacheEntryChunkT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->chunkSize = dataSize;
  tmp->next = NULL;
  return tmp;
}

CacheNodeT *CacheNode_new(const size_t dataSize) {
  CacheNodeT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  memset(tmp, 0, sizeof(*tmp));
  return tmp;
}

CacheT *CacheT_new(const size_t cacheSizeLimit) {
  CacheT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->cacheSizeLimit = cacheSizeLimit;
  tmp->entries = NULL;

  if (pthread_mutexattr_init(&attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_init failed %s", strerror(errno)
    );
    goto destroyAtMalloc;
  }
  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_settype failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  if (pthread_mutex_init(&tmp->entriesMutex, &attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutex_init failed %s", strerror(errno)
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

CacheEntryT *CacheEntryT_insertData(char *data, CacheEntryT *entry) {
}

void deleteEntry(CacheEntryT *entry) {
  pthread_mutex_lock(&cacheMutex);

  CacheEntryT *cur = cache;
  CacheEntryT *prev = NULL;
  // todo
  while (cur != NULL) {
    if (cur == entry) {
      if (prev == NULL) {
        cache = cur->next;
      } else {
        prev->next = cur->next;
      }

      free(cur->data);
      pthread_mutex_destroy(&cur->dataMutex);
      pthread_cond_destroy(&cur->dataAvailable);
      free(cur);

      cacheSize--;
      pthread_mutex_unlock(&cacheMutex);
      return;
    }

    prev = cur;
    cur = cur->next;
  }

  pthread_mutex_unlock(&cacheMutex);
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

CacheEntryT *getCacheEntry(
  const char *url, const CacheT *cache
) {
  for (
    CacheNodeT *node = cache->;
    node != NULL;
    node = node->next
  ) {
    if (strcmp(node->url, url) == 0) return node;
  }
  return NULL;
}

CacheEntryT *putCacheEntry(CacheT *cache, CacheEntryT *entry) {
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
