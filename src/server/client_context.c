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
#define MAX_RESPONSE_LEN 16
#define SUCCESS_STATUS 200

int getSocketOfRemote(const char *host, int port);

void handleConnection(
  CacheManagerT *cacheManager, BufferT *buffer, int clientSocket
);

static int ableForCashing(const char *mehtod) {
  if (strcmp(mehtod, "GET") == 0) return 1;
  return 0;
}

void *clientConnectionHandler(void *args) {
  ClientContextArgsT *contextArgs  = args;
  BufferT *           buffer       = BufferT_new(BUFFER_SIZE);
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
  logInfo("client with socket : %d finished", clientSocket);
  BufferT_delete(buffer);
  close(clientSocket);
  free(contextArgs);
  return NULL;
}

void waitFirstChunkData(const volatile CacheNodeT *cache) {
  CacheEntryT *entry = cache->entry;
  if (entry->status != Failed && entry->dataChunks == NULL) {
    int ret = pthread_mutex_lock(&entry->dataMutex);
    CHECK_RET("pthread_mutex_lock", ret);

    while (entry->status != Failed && entry->dataChunks == NULL) {
      ret = pthread_cond_wait(&entry->dataCond, &entry->dataMutex);
      CHECK_RET("pthread_cond_wait", ret);
    }

    ret = pthread_mutex_unlock(&entry->dataMutex);
    CHECK_RET("pthread_mutex_unlock", ret);
  }
}

int sendBufferAndForwardData(
  BufferT * buffer,
  const int src,
  const int dest
) {
  ssize_t ret = sendNWithTimeout(
    dest, buffer->data, buffer->occupancy,SEND_RECV_TIMEOUT
  );
  if (ret == ERROR || ret == TIMEOUT_EXPIRED) {
    logError(
      "%s:%d failed to forward request", __FILE__, __LINE__, strerror(errno)
    );
    return ret;
  }
  ret = forwardDataWithTimeout(
    src, dest, SEND_RECV_TIMEOUT, buffer
  );

  if (ret != SUCCESS) {
    return ERROR;
  }
  return SUCCESS;
}

/**
 * receive part to @code buffer
 * of response and check status
 * @param remoteSocket
 * @param buffer
 * @return ERROR if error occurs, else response status
 */
static int receiveCheckResponseStatus(const int remoteSocket, BufferT *buffer) {
  char   protocolBuffer[MAX_RESPONSE_LEN]; // Буфер для данных из сокета
  int    statusCode;
  size_t received = recvNWithTimeout(
    remoteSocket, buffer->data, buffer->occupancy, SEND_RECV_TIMEOUT
  );
  if (errno != 0) {
    logError(
      "%s:%d failed to receive response", __FILE__, __LINE__, strerror(errno)
    );
    return ERROR;
  }

  buffer->occupancy = received;

  if (sscanf(buffer->data, "%s %d", protocolBuffer, &statusCode) == 2) {
    if (strncmp(protocolBuffer, "HTTP/", 5) == 0) {
      return statusCode; // Возвращаем код состояния
    }
  }

  return ERROR;
}

/**
 *
 * @param buffer
 * @param host
 * @param port
 * @param clientSocket
 * @param url
 * @param responseStatus
 * @return
 */
CacheNodeT *startDataUpload(
  BufferT *   buffer,
  const char *host,
  const int   port,
  const int   clientSocket,
  const char *url,
  int *       responseStatus
) {
  *responseStatus    = ERROR;
  CacheNodeT * node  = NULL;
  CacheEntryT *entry = NULL;

  const int remoteSocket = getSocketOfRemote(host, port);
  if (remoteSocket < 0) {
    logError("%s, %d failed getSocketOfRemote of host:port %s:%d",
             __FILE__, __LINE__, host, port);
    sendError(clientSocket, BadGatewayStatus, FailedToConnectRemoteServer);
    return NULL;
  }

  int ret = sendBufferAndForwardData(buffer, clientSocket, remoteSocket);
  if (ret != SUCCESS) {
    goto onFailure;
  }

  const int status = receiveCheckResponseStatus(remoteSocket, buffer);
  if (status < 0) {
    goto onFailure;
  }
  *responseStatus = status;
  if (status != SUCCESS_STATUS) {
    sendBufferAndForwardData(buffer, remoteSocket, clientSocket);
    goto onFailure;
  }

  node  = CacheNodeT_new();
  entry = CacheEntryT_new();

  if (node == NULL) {
    logError("%s:%d CacheNodeT_new %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }
  if (entry == NULL) {
    logError("%s:%d CacheEntryT_new %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }
  entry->url = strdup(url);
  if (entry->url == NULL) {
    logError("%s:%d strdup %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  ret = handleFileUpload(entry, buffer, clientSocket, remoteSocket);
  if (ret != SUCCESS) {
    logError("%s:%d handleFileUpload %s",__FILE__,__LINE__, strerror(errno));
    goto onFailure;
  }

  node->entry = entry;
  return node;

onFailure:
  CacheEntryT_delete(entry);
  CacheNodeT_delete(node);
  close(remoteSocket);
  return NULL;
}

static volatile CacheEntryChunkT *waitFirstChunk(const CacheNodeT *node) {
  volatile CacheEntryT *     entry    = node->entry;
  volatile CacheEntryChunkT *curChunk = NULL;
  while (1) {
    curChunk            = entry->dataChunks;
    CacheStatusT status = entry->status;
    if (status == Failed || curChunk != NULL) {
      break;
    }

    int ret = pthread_mutex_lock((pthread_mutex_t *) &entry->dataMutex);
    CHECK_RET("pthread_mutex_lock", ret);
    while (1) {
      curChunk = entry->dataChunks;
      status   = entry->status;
      if (status == Failed || curChunk != NULL) {
        break;
      }

      ret = pthread_cond_wait(
        (pthread_cond_t *) &entry->dataCond,
        (pthread_mutex_t *) &entry->dataMutex
      );
      CHECK_RET("pthread_cond_wait", ret);
    }
    ret = pthread_mutex_unlock((pthread_mutex_t *) &entry->dataMutex);
    CHECK_RET("pthread_mutex_unlock", ret);
  }
  return curChunk;
}

static size_t readSendIncomingData(
  const volatile CacheEntryT *     cacheEntry,
  const volatile CacheEntryChunkT *chunk,
  const int                        clientSocket,
  const size_t                     written) {
  size_t curMaxWritten = written;
  for (
    size_t newMaxWritten = chunk->curDataSize;
    chunk->next == NULL && newMaxWritten == written
    && cacheEntry->status == InProcess;
    newMaxWritten = chunk->curDataSize
  ) {
    int ret = pthread_mutex_lock((pthread_mutex_t *) &cacheEntry->dataMutex);
    CHECK_RET("pthread_mutex_lock", ret);
    for (
      newMaxWritten = chunk->curDataSize;
      chunk->next == NULL && newMaxWritten == written
      && cacheEntry->status == InProcess;
      newMaxWritten = chunk->curDataSize
    ) {
      ret = pthread_cond_wait(
        (pthread_cond_t *) &cacheEntry->dataCond,
        (pthread_mutex_t *) &cacheEntry->dataMutex
      );
      CHECK_RET("pthread_cond_wait", ret);
    }
    ret = pthread_mutex_unlock((pthread_mutex_t *) &cacheEntry->dataMutex);
    CHECK_RET("pthread_mutex_unlock", ret);

    errno = 0;
    sendN(clientSocket, chunk->data + written, newMaxWritten - written);
    if (errno != 0) {
      logError("%s:%d sendN %s",__FILE__,__LINE__, strerror(errno));
      return curMaxWritten;
    }
    curMaxWritten = newMaxWritten;
  }
  return curMaxWritten;
}

static int readDataFromChunks(
  const int clientSocket, volatile CacheEntryT *cacheEntry
) {
  for (
    const volatile CacheEntryChunkT *chunk = cacheEntry->dataChunks;
    chunk != NULL && cacheEntry->status != Failed;
    chunk = chunk->next
  ) {
    size_t maxWritten = 0;
    while (chunk->next == NULL && cacheEntry->status == InProcess) {
      const size_t newMaxWritten = readSendIncomingData(
        cacheEntry, chunk, clientSocket, maxWritten
      );
      if (errno != 0) {
        return ERROR;
      }
      maxWritten = newMaxWritten;
    }

    if (cacheEntry->status == Failed) return ERROR;
    if (maxWritten != chunk->curDataSize) {
      const size_t sent = sendN(
        clientSocket, chunk->data + maxWritten, chunk->curDataSize - maxWritten
      );
      if (errno != 0) {
        logError("%s:%d sendN %s",__FILE__,__LINE__, strerror(errno));
        return ERROR;
      }
      if (sent == 0) {
        return SUCCESS;
      }
    }
  }

  logInfo(
    "client %d receive data with status %d",
    clientSocket, cacheEntry->status
  );

  if (cacheEntry->status == Failed) {
    return ERROR;
  }
  return SUCCESS;
}

int readAndSendFromCache(const int clientSocket, CacheNodeT *node) {
  waitFirstChunkData(node);
  int                              retVal     = SUCCESS;
  const volatile CacheEntryChunkT *firstChunk = waitFirstChunk(node);
  if (firstChunk != NULL) {
    retVal = readDataFromChunks(clientSocket, node->entry);
  }
  if (retVal == ERROR) {
    logError("xyi");
  }
  logInfo("client with socket : %d finished", clientSocket);
  return retVal;
}

int sendWithCachingIfNecessary(
  CacheManagerT *cacheManager,
  BufferT *      buffer,
  const char *   host,
  const int      port,
  const int      clientSocket,
  const char *   url
) {
  int ret = pthread_mutex_lock(&cacheManager->entriesMutex);
  CHECK_RET("pthread_mutex_lock", ret);

  CacheNodeT *node = CacheManagerT_get_CacheNodeT(cacheManager, url);

  if (node == NULL) {
    int responseStatus = 0;

    node = startDataUpload(
      buffer, host, port, clientSocket, url, &responseStatus
    );
    if (node == NULL) {
      int retval = 0;
      if (responseStatus < 0) {
        retval = errno;
      } else if (responseStatus != SUCCESS_STATUS) {
        retval = 0;
      }

      ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
      CHECK_RET("pthread_mutex_unlock", ret);
      return retval;
    }

    CacheManagerT_put_CacheNodeT(cacheManager, node);
  }

  CacheEntryT_acquire(node->entry);
  ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
  CHECK_RET("pthread_mutex_lock", ret);

  const int retValue = readAndSendFromCache(clientSocket, node);
  CacheEntryT_release(node->entry);
  return retValue;
}

void handleConnection(CacheManagerT *cacheManager,
                      BufferT *      buffer,
                      const int      clientSocket
) {
  char      protocol[PROTOCOL_MAX_LEN];
  char      method[METHOD_MAX_LEN];
  char *    url       = NULL;
  char *    host      = NULL;
  char *    path      = NULL;
  const int bytesRead = recvNWithTimeout(
    clientSocket, buffer->data, buffer->maxSize - 1, SEND_RECV_TIMEOUT
  );
  if (bytesRead <= 0) {
    logError("%s:%d failed to read request %s",
             __FILE__, __LINE__, strerror(errno));
    goto notifyInternalError;
  }
  logDebug("%s:%d recvN bytesRead = %d", __FILE__, __LINE__, bytesRead);
  buffer->occupancy       = bytesRead;
  buffer->data[bytesRead] = '\0';
  url                     = malloc(URL_MAX_LEN * sizeof(*url));
  host                    = malloc(URL_MAX_LEN * sizeof(*host));
  path                    = malloc(URL_MAX_LEN * sizeof(*path));
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
    sendWithCachingIfNecessary(cacheManager, buffer, host, port, clientSocket, url);
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
