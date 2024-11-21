#include "cache.h"

#include <stdlib.h>


CacheEntryChunkT *CacheEntryChunk_new(const size_t dataSize) {
  CacheEntryChunkT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->totalDataSize = dataSize;
  tmp->next = NULL;
  return tmp;
}

void CacheEntryChunk_delete(CacheEntryChunkT *chunk) {
  if (chunk == NULL) return;
  for (CacheEntryChunkT *cur = chunk; cur != NULL;) {
    CacheEntryChunkT *todel = cur;
    cur = cur->next;

    free(todel->data);
    free(todel);
  }
}
