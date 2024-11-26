#include <assert.h>

#include "cache.h"
#include "../utils/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "../server/proxy.h"

CacheEntryT *CacheEntryT_new() {
  CacheEntryT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->status = InProcess;
  tmp->dataChunks = NULL;
  tmp->lastChunk = NULL;
  tmp->downloadedSize = 0;

  pthread_mutexattr_t attr;
  if (pthread_cond_init(&tmp->dataCond, NULL) != 0) {
    logFatal("[CacheEntryT_new] pthread_cond_init", strerror(errno));
    goto destroyAtMalloc;;
  }
  if (pthread_mutexattr_init(&attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_init failed %s", strerror(errno)
    );
    goto destroyAtCond;
  }
  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutexattr_settype failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  if (pthread_mutex_init(&tmp->dataMutex, &attr) != 0) {
    logFatal(
      "[CacheEntryT_new] pthread_mutex_init failed %s", strerror(errno)
    );
    goto destroyAtMutexAtrs;
  }
  return tmp;


destroyAtMutexAtrs:
  pthread_mutexattr_destroy(&attr);
destroyAtCond:
  pthread_cond_destroy(&tmp->dataCond);
destroyAtMalloc:
  free(tmp);
  tmp = NULL;
  return tmp;
}

void CacheEntryT_release(CacheEntryT *entry) {
  int ret = pthread_mutex_lock(&entry->dataMutex);
  CHECK_ERROR("pthread_mutex_lock", ret);

  entry->usersQ--;
  gettimeofday(&entry->lastUpdate, NULL);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  CHECK_RET(pthread_mutex_unlock, ret);
}

void CacheEntryT_acquire(CacheEntryT *entry) {
  int ret = pthread_mutex_lock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_lock", ret);

  entry->usersQ++;
  gettimeofday(&entry->lastUpdate, NULL);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_unlock", ret);
}

void CacheEntryT_delete(CacheEntryT *entry) {
  if (entry == NULL) return;
  free(entry->url);
  for (volatile CacheEntryChunkT *cur = entry->dataChunks;
       cur != NULL;) {
    CacheEntryChunkT *tmp = (CacheEntryChunkT *) cur;
    CacheEntryChunkT_delete(tmp);
    cur = cur->next;
  }
  free(entry->url);
  if (pthread_mutex_destroy(&entry->dataMutex) != 0) abort();
  if (pthread_cond_destroy(&entry->dataCond) != 0)abort();
}

void CacheEntryT_updateStatus(CacheEntryT *      entry,
                              const CacheStatusT status) {
  if (entry == NULL) return;
  int ret = pthread_mutex_lock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_lock", ret);

  entry->status = status;
  gettimeofday(&entry->lastUpdate, NULL);
  ret = pthread_cond_broadcast(&entry->dataCond);

  CHECK_RET("pthread_cond_broadcast", ret);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_unlock", ret);
}

CacheEntryChunkT *CacheEntryT_appendData(
  CacheEntryT *      entry,
  const char *       data,
  const size_t       dataSize,
  const CacheStatusT status
) {
  if (dataSize <= 0) return (CacheEntryChunkT *) entry->lastChunk;
  size_t                     added = 0;
  volatile CacheEntryChunkT *retval = NULL;
  int                        ret = pthread_mutex_lock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_lock", ret);
  if (entry->dataChunks == NULL) {
    CacheEntryChunkT *iniChunk = CacheEntryChunkT_new(kDefCacheChunkSize);
    if (iniChunk == NULL) {
      entry->status = Failed;
      goto onExit;
    }
    CacheEntryT_append_CacheEntryChunkT(entry, iniChunk);
  }

  retval = entry->lastChunk;
  while (added < dataSize) {
    assert(retval->curDataSize <= retval->maxDataSize);

    const size_t freeSpace = retval->maxDataSize - retval->curDataSize;
    if (freeSpace == 0) {
      retval = CacheEntryChunkT_new(kDefCacheChunkSize);
      if (retval == NULL) {
        logError("%s:%d cache entry chunk allocation failed: %s",
                 __LINE__, __FILE__, strerror(errno));
        entry->status = Failed;
        goto onExit;
      }
      CacheEntryT_append_CacheEntryChunkT(entry, (CacheEntryChunkT *) retval);
    } else {
      const size_t toCopy = dataSize - added < freeSpace
                              ? dataSize - added
                              : freeSpace;
      void *      dest = retval->data + retval->curDataSize;
      const void *src = data + added;

      memcpy(dest, src, toCopy);

      retval->curDataSize += toCopy;
      added += toCopy;
    }
  }
  entry->status = status;

onExit:
  ret = pthread_cond_broadcast(&entry->dataCond);
  CHECK_RET("pthread_cond_broadcast", ret);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  CHECK_RET("pthread_mutex_unlock", ret);
  return (CacheEntryChunkT *) retval;
}
