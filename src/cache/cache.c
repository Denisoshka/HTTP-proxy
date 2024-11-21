#include "cache.h"

#include <assert.h>

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

static bool isCacheNodeValid(
  const CacheManagerT *manager, CacheEntryT *entry, const struct timeval now
) {
  const long elapsedMs = (now.tv_sec - entry->lastUpdate.tv_sec) * 1000
                         + (now.tv_usec - entry->lastUpdate.tv_usec) / 1000;
  entry->lastUpdate.tv_sec = now.tv_sec;
  return elapsedMs <= manager->entryThreshold;
}

CacheManagerT *CacheManagerT_new() {
  CacheManagerT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }

  tmp->nodes = NULL;
  pthread_mutexattr_t attr;

  int ret = pthread_mutexattr_init(&attr);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutexattr_init failed %s", strerror(errno)
    );
    abort();
  }

  ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutexattr_settype failed %s", strerror(errno)
    );
    abort();
  }
  ret = pthread_mutex_init(&tmp->entriesMutex, &attr);
  if (ret != 0) {
    logFatal(
      "[CacheT] pthread_mutex_init failed %s", strerror(errno)
    );
    abort();
  }
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
      gettimeofday(&node->entry->lastUpdate, NULL);

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
  const CacheManagerT *cache, CacheNodeT *node
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
  gettimeofday(&node->entry->lastUpdate, NULL);

  ret = pthread_mutex_unlock(&node->entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_unlock failed %s",
      strerror(errno)
    );
    abort();
  }
}

void CacheEntryT_append_CacheEntryChunkT(
  CacheEntryT *entry, CacheEntryChunkT *chunk, const int isLast
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

  entry->lastChunk->next = chunk;
  entry->downloadedSize += chunk->totalDataSize;
  gettimeofday(&entry->lastUpdate, NULL);

  assert(entry->downloadFinished && isLast);

  entry->downloadFinished = isLast;

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
}

void CacheManagerT_checkAndRemoveExpired_CacheNodeT(CacheManagerT *manager) {
  int ret = pthread_mutex_lock(&manager->entriesMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_checkAndRemoveExpired_CacheNodeT] pthread_mutex_lock : %s",
      strerror(errno)
    );
    abort();
  }

  struct timeval checkStart;
  gettimeofday(&checkStart, NULL);

  for (CacheNodeT **current = &manager->nodes; (*current) != NULL;) {
    CacheNodeT *node = *current;
    ret = pthread_mutex_lock(&node->entry->dataMutex);
    if (ret != 0) {
      logFatal(
        "[CacheManagerT_checkAndRemoveExpired_CacheNodeT] pthread_mutex_lock : %s",
        strerror(errno)
      );
      abort();
    }
    if (!isCacheNodeValid(manager, node->entry, checkStart)
        && node->usersQ == 0) {
      *current = node->next;
      CacheNodeT_delete(node);
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
  ret = pthread_mutex_unlock(&manager->entriesMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_checkAndRemoveExpired_CacheNodeT] pthread_mutex_unlock : %s",
      strerror(errno)
    );
    abort();
  }
}
