#include "cache.h"

#include <stdlib.h>
#include <string.h>

CacheNodeT *CacheNodeT_new(const size_t dataSize) {
  CacheNodeT *tmp = malloc(sizeof(*tmp));
  if (tmp == NULL) {
    return NULL;
  }
  memset(tmp, 0, sizeof(*tmp));
  return tmp;
}

void CacheNodeT_delete(const CacheNodeT *node) {
  if (node == NULL) return;
  CacheEntryT_delete(node->entry);
}
