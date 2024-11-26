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

#define METHOD_MAX_LEN 16
#define URL_MAX_LEN 2048
#define PROTOCOL_MAX_LEN 16
#define RECT_TIMEOUT 5000

int getSocketOfRemote(const char *host, int port);

void handleConnection(
  CacheManagerT *cacheManager, BufferT *buffer, int clientSocket
);

static int ableForCashing(const char *mehtod) {
  if (strcmp(mehtod, "GET") == 0) return 1;
  return 0;
}

void *clientConnectionHandler(void *args) {
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
  return NULL;
}

void waitFirstChunkData(CacheNodeT *cache) {
  CacheEntryT *entry = cache->entry;
  if (entry->status != Failed && entry->dataChunks == NULL) {
    if (pthread_mutex_lock(&entry->dataMutex) != 0) { abort(); }
    while (entry->status != Failed && entry->dataChunks == NULL) {
      if (pthread_cond_wait(&entry->dataCond, &entry->dataMutex) != 0) {
        abort();
      }
    }
    if (pthread_mutex_unlock(&entry->dataMutex) != 0) { abort(); }
  }
}

CacheNodeT *startDataUpload(
  BufferT *   buffer,
  const char *host,
  const int   port,
  const int   clientSocket,
  const char *url
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

  const int ret = handleFileUpload(entry, buffer, host, port, clientSocket);
  if (ret != SUCCESS) {
    logError("%s:%d handleFileUpload %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  node->entry = entry;
  return node;

onFailure:
  CacheEntryT_delete(entry);
  CacheNodeT_delete(node);
  return NULL;
}

int readAndSendFromCache(const int clientSocket, CacheNodeT *node) {
  waitFirstChunkData(node);
  int                        retVal = SUCCESS;
  CacheEntryT *              entry = node->entry;
  volatile CacheEntryChunkT *curChunk = entry->dataChunks;
  while (entry->status != Failed) {
    size_t writed = 0;
    while (curChunk->next == NULL) {
      int    ret = 0;
      size_t curMax = curChunk->curDataSize;
      if (curMax == writed) {
        ret = pthread_mutex_lock(&entry->dataMutex);
        if (ret != SUCCESS) { abort(); }
        curMax = curChunk->curDataSize;
        while (curMax == writed) {
          ret = pthread_cond_wait(&entry->dataCond, &entry->dataMutex);
          if (ret != SUCCESS) {
            abort();
          }
          curMax = curChunk->curDataSize;
        }
        ret = pthread_mutex_unlock(&entry->dataMutex);
        if (ret != 0) { abort(); }
      }
      ret = sendN(clientSocket, curChunk->data + writed, curMax - writed);
      if (ret < 0) {
        logError("%s:%d sendN %s",__FILE__, __LINE__, strerror(ret));
        retVal = ret;
      }
      writed = curMax;
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
    if (entry->status == Success && curChunk->next == NULL) {
      break;
    }
    curChunk = curChunk->next;
  }
  return retVal;
}

int sendWithCaching(
  CacheManagerT *cacheManager,
  BufferT *      buffer,
  const char *   host,
  const int      port,
  const int      clientSocket,
  const char *   url
) {
  int ret = pthread_mutex_lock(&cacheManager->entriesMutex);
  if (ret != 0) {
    logFatal("%s:%d pthread_mutex_lock failed %s",
             __FILE__, __LINE__, strerror(ret));
    abort();
  }
  CacheNodeT *node = CacheManagerT_get_CacheNodeT(cacheManager, url);

  if (node == NULL) {
    node = startDataUpload(buffer, host, port, clientSocket, url);
    if (node == NULL) {
      ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
      if (ret != 0) {
        logFatal("%s:%d pthread_mutex_unlock failed %s",
                 __FILE__, __LINE__, strerror(ret));
        abort();
      }
      return errno;
    }
    CacheManagerT_put_CacheNodeT(cacheManager, node);
  }

  CacheEntryT_acquire(node->entry);
  ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
  if (ret != 0) {
    logFatal("%s:%d pthread_mutex_lock failed %s",
             __FILE__, __LINE__, strerror(ret));
    abort();
  }

  int retvalue = readAndSendFromCache(clientSocket, node);
  CacheEntryT_release(node->entry);
  return retvalue;
}

void handleConnection(CacheManagerT *cacheManager,
                      BufferT *      buffer,
                      const int      clientSocket
) {
  char      protocol[PROTOCOL_MAX_LEN];
  char      method[METHOD_MAX_LEN];
  char *    url = NULL;
  char *    host = NULL;
  char *    path = NULL;
  const int bytesRead = recvNWithTimeout(
    clientSocket, buffer->data, buffer->maxSize - 1, RECV_TIMEOUT
  );
  if (bytesRead <= 0) {
    logError("%s:%d failed to read request %s",
             __FILE__, __LINE__, strerror(errno));
    goto notifyInternalError;
  }
  logDebug("%s:%d recvN bytesRead = %d", __FILE__, __LINE__, bytesRead);
  buffer->occupancy = bytesRead;
  buffer->data[bytesRead] = '\0';
  url = malloc(URL_MAX_LEN * sizeof(*url));
  host = malloc(URL_MAX_LEN * sizeof(*host));
  path = malloc(URL_MAX_LEN * sizeof(*path));
  if (url == NULL || host == NULL || path == NULL) {
    logError("%s:%d failed to allocate memory",
             __FILE__, __LINE__, strerror(errno));
    goto notifyInternalError;
  }

  int port;
  sscanf(buffer->data, "%s %s %s", method, url, protocol);
  const int ret = parseURL(url, host, path, &port);
  if (ret != SUCCESS) {
    logError("%s, %d failed to parse URL %s",__FILE__, __LINE__, url);
    sendError(clientSocket, BadRequestStatus, InvalidRequestMessage);
    goto destroyContext;
  }

  if (ableForCashing(method)) {
    sendWithCaching(cacheManager, buffer, host, port, clientSocket, url);
  } else {
    // todo
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
  buffer->maxSize = maxOccupancy;
  return buffer;
}

void BufferT_delete(BufferT *buffer) {
  if (buffer == NULL) return;
  free(buffer->data);
  free(buffer);
}
