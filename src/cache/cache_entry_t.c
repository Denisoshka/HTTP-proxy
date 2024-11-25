#include "cache.h"
#include "../utils/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_lock failed %s",
      strerror(errno)
    );
    abort();
  }

  entry->usersQ--;
  gettimeofday(&entry->lastUpdate, NULL);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_acquire_CacheNodeT] pthread_mutex_unlock failed %s",
      strerror(errno)
    );
    abort();
  }
}

void CacheEntryT_acquire(CacheEntryT *entry) {
  int ret = pthread_mutex_lock(&entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheEntryT_acquire] pthread_mutex_lock failed %s",
      strerror(errno)
    );
    abort();
  }

  entry->usersQ++;
  gettimeofday(&entry->lastUpdate, NULL);

  ret = pthread_mutex_unlock(&entry->dataMutex);
  if (ret != 0) {
    logFatal(
      "[CacheManagerT_get_CacheNodeT] pthread_mutex_unlock failed %s",
      strerror(errno)
    );
    abort();
  }
}


void CacheEntryT_delete(CacheEntryT *entry) {
  if (entry == NULL) return;
  free(entry->url);
  for (volatile CacheEntryChunkT *cur = entry->dataChunks;
       cur != NULL;) {
    volatile CacheEntryChunkT *tmp = cur;
    CacheEntryChunkT_delete(tmp);
    cur = cur->next;
  }
  free(entry->url);
  if (pthread_mutex_destroy(&entry->dataMutex) != 0) abort();
  if (pthread_cond_destroy(&entry->dataCond) != 0)abort();
}

void CacheEntryT_updateStatus(CacheEntryT *               entry,
                              const enum CacheEntryStatus status) {
  if (entry == NULL) return;
  int ret = pthread_mutex_lock(&entry->dataMutex);
  if (ret != 0) {
    logFatal("[CacheEntryT_uploadFinished] pthread_mutex_lock %s",
             strerror(errno));
    abort();
  }
  entry->status = status;
  gettimeofday(&entry->lastUpdate, NULL);
  ret = pthread_cond_broadcast(&entry->dataCond);
  if (ret != 0) {
    logFatal("[CacheEntryT_uploadFinished] pthread_cond_broadcast %s",
             strerror(errno));
    abort();
  }
  ret = pthread_mutex_unlock(&entry->dataMutex);
  if (ret != 0) {
    logFatal("[CacheEntryT_uploadFinished] pthread_mutex_unlock %s",
             strerror(errno));
    abort();
  }
}
