#include "cache.h"
#include "../utils/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

CacheEntryT *CacheEntryT_new(const size_t cacheSizeLimit) {
  CacheEntryT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }

  tmp->data = NULL;
  tmp->lastChunk = NULL;
  tmp->downloadedSize = 0;
  tmp->downloadFinished = 0;

  pthread_mutexattr_t attr;
  if (pthread_cond_init(&tmp->dataCond, NULL) != 0) {
    logFatal("[CacheT] pthread_cond_init", strerror(errno));
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

void CacheEntryT_delete(CacheEntryT *entry) {
  if (entry == NULL) return;

}
