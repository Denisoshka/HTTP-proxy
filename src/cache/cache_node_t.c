#include "cache.h"
#include "../utils/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

CacheNodeT *CacheNodeT_new() {
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
