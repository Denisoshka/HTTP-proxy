#include "cache.h"

#include <stdlib.h>


CacheEntryChunkT *CacheEntryChunk_new(const size_t dataSize) {
  CacheEntryChunkT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  tmp->chunkSize = dataSize;
  tmp->next = NULL;
  return tmp;
}
