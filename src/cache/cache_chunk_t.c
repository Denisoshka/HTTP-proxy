#include "cache.h"

#include <stdlib.h>


CacheEntryChunkT *CacheEntryChunkT_new(const size_t dataSize) {
  CacheEntryChunkT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->data = malloc(dataSize * sizeof(*tmp->data));
  tmp->maxDataSize = dataSize;
  tmp->curDataSize = 0;
  tmp->next = NULL;
  return tmp;
}

void CacheEntryChunkT_delete(CacheEntryChunkT *chunk) {
  if (chunk == NULL) return;

  free(chunk->data);
  free(chunk);
}
