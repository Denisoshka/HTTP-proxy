#include "cache.h"

#include <assert.h>

#include "../utils/log.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/**
 * use under `CacheManagerT->entriesMutex`
 * @param cache
 * @param url cache url
 * @return `CacheNodeT *` if contains else `null`
 */
CacheNodeT *CacheManagerT_get_CacheNodeT(
  const CacheManagerT *cache, const char *url
) {
  for (
    CacheNodeT *node = cache->nodes;
    node != NULL;
    node = node->next
  ) {
    if (strcmp(node->entry->url, url) == 0) {
      return node;
    }
  }
  return NULL;
}

void CacheManagerT_put_CacheNodeT(CacheManagerT *cache, CacheNodeT *node) {
  if (node == NULL) return;

  if (cache->nodes == NULL) {
    cache->nodes = node;
    cache->lastNode = node;
  } else {
    cache->lastNode->next = node;
    cache->lastNode = node;
  }
}

void CacheEntryT_append_CacheEntryChunkT(
  CacheEntryT *entry, CacheEntryChunkT *chunk
) {
  if (chunk == NULL) return;

  if (entry->dataChunks == NULL) {
    entry->lastChunk = chunk;
    entry->dataChunks = chunk;
    return;
  }

  entry->lastChunk->next = chunk;
  entry->lastChunk = chunk;
  entry->downloadedSize += chunk->maxDataSize;
  gettimeofday(&entry->lastUpdate, NULL);
}

void CacheManagerT_checkAndRemoveExpired_CacheNodeT(CacheManagerT *manager) {
  int ret = pthread_mutex_lock(&manager->entriesMutex);
  CHECK_RET("pthread_mutex_lock", ret);

  struct timeval checkStart;
  gettimeofday(&checkStart, NULL);

  for (CacheNodeT **current = &manager->nodes; (*current) != NULL;) {
    CacheNodeT *node = *current;
    ret = pthread_mutex_trylock(&node->entry->dataMutex);
    if (ret == EBUSY) continue;
    CHECK_RET("pthread_mutex_trylock", ret);

    if (
      !isCacheNodeValid(manager, node->entry, checkStart)
      && node->entry->usersQ == 0
    ) {
      *current = node->next;
      CacheNodeT_delete(node);
    } else {
      current = &node->next;
    }
    ret = pthread_mutex_unlock(&node->entry->dataMutex);
    CHECK_RET("pthread_mutex_unlock", ret);
  }

  ret = pthread_mutex_unlock(&manager->entriesMutex);
  CHECK_RET("pthread_mutex_unlock", ret);
}

CacheEntryT *CacheEntryT_new_withUrl(const char *url) {
  CacheEntryT *entry = CacheEntryT_new();
  if (entry == NULL) {
    logError(
      "[handleConnection] CacheEntryT_new failed : %s", strerror(errno)
    );
    return NULL;
  }
  entry->url = strdup(url);
  if (entry->url == NULL) {
    logError(
      "[handleConnection] strdup failed : %s", strerror(errno)
    );
    CacheEntryT_delete(entry);
    return NULL;
  }
  return entry;
}
