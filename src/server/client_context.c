#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "proxy.h"
#include "../utils/log.h"

#define ERROR (-1)
#define SUCCESS 0

#define METHOD_MAX_LEN 16
#define URL_MAX_LEN 2048
#define PROTOCOL_MAX_LEN 16

int getSocketOfRemote(const char *host, int port);

void handleConnection(
  CacheManagerT *cacheManager, BufferT *buffer, int clientSocket
);

static int ableForCashing(const char *mehtod) {
  if (strcmp(mehtod, "GET") == 0) return 1;
  return 0;
}

void handleConnectionStartUp(void *args) {
  ClientContextArgsT *contextArgs = args;
  BufferT *           buffer = BufferT_new(BUFFER_SIZE);
  const int           clientSocket = contextArgs->clientSocket;
  CacheManagerT *     cacheManager = contextArgs->cacheManager;

  if (buffer == NULL) {
    logError("%s:%d failed to allocate buffer",
             __FILE__, __LINE__, strerror(errno));
    sendError(clientSocket, InternalErrorStatus, "");
    goto destroyContext;
  }
  handleConnection(cacheManager, buffer, clientSocket);
destroyContext:
  BufferT_delete(buffer);
  free(contextArgs);
}

void waitFirstChunkData(CacheEntryT *cache) {
  if (cache->status != Failed && cache->dataChunks == NULL) {
    if (pthread_mutex_lock(&cache->dataMutex) != 0) { abort(); }
    while (cache->status != Failed && cache->dataChunks == NULL) {
      if (pthread_cond_wait(&cache->dataCond, &cache->dataMutex) != 0) {
        abort();
      }
    }
    if (pthread_mutex_unlock(&cache->dataMutex) != 0) { abort(); }
  }
}

CacheNodeT *startDataUpload(
  CacheManagerT *cacheManager, const int clientSocket, const char *url
) {
  CacheNodeT * node = CacheNodeT_new();
  CacheEntryT *entry = CacheEntryT_new();

  if (entry == NULL) {
    logError("%s:%d CacheEntryT_new %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }
  if (node == NULL) {
    logError("%s:%d CacheNodeT_new %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  entry->url = strdup(url);
  if (entry->url == NULL) {
    logError("%s:%d strdup %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  const int ret = handleFileUpload(entry, clientSocket);
  if (ret != SUCCESS) {
    logError("%s:%d handleFileUpload %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  node->entry = entry;
  CacheManagerT_put_CacheNodeT(cacheManager, node);
  return node;

onFailure:
  CacheEntryT_delete(entry);
  CacheNodeT_delete(node);
  return NULL;
}

int sendFromCache(
  CacheManagerT *cacheManager, const int clientSocket, const char *url) {
  int ret = pthread_mutex_lock(&cacheManager->entriesMutex);
  if (ret != 0) { abort(); }

  CacheNodeT *node = CacheManagerT_get_CacheNodeT(cacheManager, url);
  if (node == NULL) {
    node = startDataUpload(cacheManager, clientSocket, url);
    if (node == NULL) {
      ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
      if (ret != 0) { abort(); }
      return ERROR;
    }
  }

  CacheEntryT_acquire(node->entry);
  ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
  if (ret != 0) { abort(); }

  CacheEntryT *cache = node->entry;

  waitFirstChunkData(cache);

  volatile CacheEntryChunkT *curChunk = cache->dataChunks;
  int                        retVal = SUCCESS;
  while (cache->status != Failed) {
    const size_t writed = 0;
    while (curChunk->next == NULL) {
      size_t curMax = curChunk->curDataSize;
      if (curMax - writed == 0) {
        ret = pthread_mutex_lock(&cache->dataMutex);
        if (ret != SUCCESS) { abort(); }
        curMax = curChunk->curDataSize;
        if (curMax - writed == 0) {
          ret = pthread_cond_wait(&cache->dataCond, &cache->dataMutex);
          if (ret != SUCCESS) {
            abort();
          }
        }
        ret = pthread_mutex_unlock(&cache->dataMutex);
        if (ret != 0) { abort(); }
      }
      ret = sendN(clientSocket, curChunk->data + writed, curMax - writed);
      if (ret != SUCCESS) {
        logError("%s:%d sendN %s",
                 __FILE__, __LINE__, strerror(errno));
        retVal = ERROR;
      }
    }

    const ssize_t sended = sendN(
      clientSocket, curChunk->data + writed, curChunk->curDataSize - writed
    );
    if (sended == 0) {
      break;
    }
    if (sended < 0) {
      logError("%s:%d sendN %s",
               __FILE__, __LINE__, strerror(errno));
      retVal = ERROR;
      break;
    }

    if (cache->status == Success && curChunk->next == NULL) {
      break;
    }
    curChunk = curChunk->next;
  }
  CacheEntryT_release(cache);
}

void handleConnection(CacheManagerT *cacheManager,
                      BufferT *      buffer,
                      const int      clientSocket
) {
  char protocol[PROTOCOL_MAX_LEN];
  char method[METHOD_MAX_LEN];
  struct sockaddr_in server_addr;
  const int bytesRead = recvN(clientSocket, buffer, buffer->maxSize - 1);
  if (bytesRead <= 0) {
    logError("%s:%d failed to read request %s",
             __FILE__, __LINE__, strerror(errno));
    goto notifyInternalError;
  }

  buffer->occupancy = bytesRead;
  buffer->data[bytesRead] = '\0';
  char *url = malloc(URL_MAX_LEN * sizeof(*url));
  char *host = malloc(URL_MAX_LEN * sizeof(*host));
  char *path = malloc(URL_MAX_LEN * sizeof(*path));
  if (url == NULL || host == NULL || path == NULL) {
    logError("%s:%d failed to allocate memory",
             __FILE__, __LINE__,errno(errno));
    goto notifyInternalError;
  }

  int port;
  int ret = parseURL(url, host, path, &port);
  if (ret != SUCCESS) {
    logError("%s, %d failed to parse URL %s",__FILE__, __LINE__, url);
    sendError(clientSocket, BadRequestStatus, InvalidRequestMessage);
    goto destroyContext;
  }


  sscanf(buffer->data, "%s %s %s", method, url, protocol);
  if (ableForCashing(method)) {
    sendFromCache(cacheManager, clientSocket, url);
  } else {
  }

notifyInternalError:
  sendError(clientSocket, InternalErrorStatus, "");
destroyContext:
  free(url);
  free(host);
  free(path);
}

BufferT *BufferT_new(const size_t maxOccupancy) {
  BufferT *buffer = malloc(sizeof(*buffer));
  if (buffer == NULL) {
    return NULL;
  }
  buffer->data = malloc(sizeof(*buffer->data) * maxOccupancy);
  if (buffer->data == NULL) {
    free(buffer);
    return NULL;
  }
  return buffer;
}

void BufferT_delete(BufferT *buffer) {
  if (buffer == NULL) return;
  free(buffer->data);
  free(buffer);
}
