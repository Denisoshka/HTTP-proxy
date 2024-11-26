#include "proxy.h"
#include "../utils/log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct UploadArgs {
  CacheEntryT *entry;
  BufferT *    buffer;
  int          remoteSocket;
  int          clientSocket;
} UploadArgsT;

static CacheEntryChunkT *fillCache(CacheEntryChunkT *startChunk,
                                   CacheEntryT *     entry,
                                   const BufferT *   buffer) {
  size_t            added = 0;
  CacheEntryChunkT *curChunk = startChunk;
  while (added != buffer->occupancy) {
    const size_t dif = curChunk->maxDataSize - curChunk->curDataSize;
    if (dif == 0) {
      CacheEntryT_append_CacheEntryChunkT(entry, curChunk);
      curChunk = CacheEntryChunkT_new(kDefCacheChunkSize);
      if (curChunk == NULL) {
        return NULL;
      }
    } else {
      memcpy(
        curChunk->data + curChunk->curDataSize, buffer->data + added, dif
      );
      curChunk->curDataSize += dif;
      added += dif;
    }
  }
  return curChunk;
}

static CacheEntryChunkT *readSendRestBytes(
  const int    clientSocket,
  const int    remoteSocket,
  CacheEntryT *entry,
  BufferT *    buffer
) {
  const ssize_t written = sendN(remoteSocket, buffer->data, buffer->occupancy);
  if (written < 0) {
    return NULL;
  }
// todo memory leak
  CacheEntryChunkT *curChunk = CacheEntryChunkT_new(kDefCacheChunkSize);
  if (curChunk == NULL) {
    return NULL;
  }
  while (1) {
    const ssize_t readed = recvNWithTimeout(
      clientSocket, buffer->data, buffer->maxSize,RECV_TIMEOUT
    );
    if (readed == ERROR) {
      return NULL;
    }

    if (readed == 0) {
      break;
    }

    buffer->occupancy = readed;

    curChunk = fillCache(curChunk, entry, buffer);
    if (curChunk == NULL) return NULL;
    CacheEntryT_updateStatus(entry, InProcess);
    const ssize_t written = sendN(
      remoteSocket, buffer->data, buffer->occupancy
    );
    if (written < 0) {
      return NULL;
    }
  }

  return curChunk;
}

char *strstrn(const char * haystack,
              const size_t haystackLen,
              const char * needle,
              const size_t needleLen) {
  if (needleLen == 0) {
    return (char *) haystack;
  }

  for (size_t i = 0; i <= haystackLen - needleLen; i++) {
    if (strncmp(&haystack[i], needle, needleLen) == 0) {
      return (char *) &haystack[i];
    }
  }

  return NULL; // Подстрока не найдена
}

/*int isStatusCode200(const char *response, ) {
  return strstrn(response, "HTTP/1.1 200 OK", ) != NULL || strstr(
           response, "HTTP/1.0 200 OK") != NULL;
}*/

void *fileUploaderStartup(void *args) {
  UploadArgsT *uploadArgs = args;
  CacheEntryT *entry = uploadArgs->entry;
  BufferT *    buffer = uploadArgs->buffer;
  const int    remoteSocket = uploadArgs->remoteSocket;
  const int    clientSocket = uploadArgs->clientSocket;

  CacheEntryChunkT *curChunk = readSendRestBytes(
    clientSocket, remoteSocket, entry, buffer
  );
  if (curChunk == NULL) {
    logError("%s:%d readRestBytes %s", __FILE__,__LINE__, strerror(errno));
    CacheEntryT_updateStatus(entry, Failed);
    goto dectroyContext;
  }

  int uploadStatus = SUCCESS;
  while (1) {
    const ssize_t readed = recvNWithTimeout(
      remoteSocket, buffer->data, buffer->maxSize, RECV_TIMEOUT
    );
    if (readed < 0) {
      logError("%s:%d recv %s",__FILE__, __LINE__, strerror(errno));
      uploadStatus = ERROR;
      break;
    }
    if (readed == 0) {
      break;
    }
    buffer->occupancy = readed;
    curChunk = fillCache(curChunk, entry, buffer);
    if (curChunk == NULL) {
      logError("%s:%d fillCache %s",__FILE__, __LINE__, strerror(errno));
      uploadStatus = ERROR;
      break;
    }
    CacheEntryT_updateStatus(entry, InProcess);
  }

  if (uploadStatus == SUCCESS) {
    CacheEntryT_updateStatus(entry, Success);
  } else {
    CacheEntryT_updateStatus(entry, Failed);
  }

dectroyContext:
  free(uploadArgs);
  return NULL;
}


int handleFileUpload(CacheEntryT *entry,
                     BufferT *    buffer,
                     const char * host,
                     const int    port,
                     const int    clientSocket) {
  const int remoteSocket = getSocketOfRemote(host, port);
  if (remoteSocket < 0) {
    logError("%s, %d failed getSocketOfRemote of host:port %s:%d",
             __FILE__, __LINE__, host, port);
    sendError(clientSocket, BadGatewayStatus, FailedToConnectRemoteServer);
    return ERROR;
  }
  pthread_t    thread;
  UploadArgsT *args = NULL;
  args = malloc(sizeof(*args));
  if (args == NULL) {
    logError("%s, %d malloc", __FILE__, __LINE__);
    goto uploadFailed;
  }
  args->buffer = buffer;
  args->remoteSocket = remoteSocket;
  args->clientSocket = clientSocket;
  args->entry = entry;
  const int ret = pthread_create(&thread,NULL, fileUploaderStartup, args);
  if (ret != 0) {
    logError("%s, %d pthread_create", __FILE__, __LINE__);
    goto uploadFailed;
  }
  if (pthread_detach(thread) != 0) {
    logError("%s, %d pthread_detach", __FILE__, __LINE__);
    abort();
  }
  return SUCCESS;

uploadFailed:
  sendError(clientSocket, InternalErrorStatus, "");
  free(args);
  return ERROR;
}
