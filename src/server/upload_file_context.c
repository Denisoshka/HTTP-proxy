#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "proxy.h"
#include "../utils/log.h"

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
  while (added != buffer->maxSize) {
    const size_t dif = curChunk->maxDataSize - curChunk->curDataSize;
    if (dif == 0) {
      CacheEntryT_append_CacheEntryChunkT(entry, curChunk, 0);
      curChunk = CacheEntryChunkT_new(kDefCacheChunkSize);
      if (curChunk == NULL) {
        return NULL;
      }
    } else {
      memcpy(curChunk->data + curChunk->curDataSize,
             buffer->data + added,
             dif
      );
      curChunk->curDataSize += dif;
      added += dif;
    }
  }
  return curChunk;
}

static CacheEntryChunkT *readRestBytes(
  const int    clientSocket,
  const int    remoteSocket,
  CacheEntryT *entry,
  BufferT *    buffer
) {
  CacheEntryChunkT *curChunk = CacheEntryChunkT_new(kDefCacheChunkSize);
  if (curChunk == NULL) {
    return NULL;
  }
  while (1) {
    const ssize_t readed = recvN(
      clientSocket, buffer->data, buffer->maxSize
    );
    if (readed < 0) {
      return NULL;
    }
    if (readed == 0) {
      break;
    }
    buffer->occupancy = readed;

    CacheEntryChunkT *value = fillCache(curChunk, entry, buffer);
    if (value == NULL) return NULL;

    const ssize_t written = sendN(
      remoteSocket, buffer->data, buffer->occupancy
    );
    if (written < 0) {
      return NULL;
    }
  }

  return curChunk;
}

char *strstrn(const char *haystack, size_t haystack_len, const char *needle,
              size_t      needle_len) {
  // Если подстрока пуста, сразу возвращаем указатель на начало строки
  if (needle_len == 0) {
    return (char *) haystack;
  }

  // Проходим по строке до максимальной длины
  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    // Сравниваем текущий участок с подстрокой
    if (strncmp(&haystack[i], needle, needle_len) == 0) {
      return (char *) &haystack[i]; // Подстрока найдена
    }
  }

  return NULL; // Подстрока не найдена
}

int isStatusCode200(const char *response, ) {
  return strstrn(response, "HTTP/1.1 200 OK", ) != NULL || strstr(
           response, "HTTP/1.0 200 OK") != NULL;
}

void *fileUploaderStartup(void *args) {
  UploadArgsT *uploadArgs = args;
  CacheEntryT *entry = uploadArgs->entry;
  BufferT *    buffer = uploadArgs->buffer;
  const int    remoteSocket = uploadArgs->remoteSocket;
  const int    clientSocket = uploadArgs->clientSocket;

  CacheEntryChunkT *curChunk = readRestBytes(
    clientSocket, remoteSocket, entry, buffer
  );
  if (curChunk == NULL) {
    logError("%s:%d readRestBytes %s", __FILE__,__LINE__, strerror(errno));
    CacheEntryT_updateStatus(entry, Failed);
    return NULL;
  }
  int failed = 0;
  while (1) {
    const ssize_t readed = recvN(
      remoteSocket, buffer->data, buffer->maxSize
    );
    if (readed < 0) {
      logError("%s:%d recv %s",__FILE__, __LINE__, strerror(errno));
      break;
    }
    if (readed == 0) {
      break;
    }
    buffer->occupancy = readed;
    curChunk = fillCache(curChunk, entry, buffer);
    if (curChunk == NULL) {
      failed = 1;
    }
  }

  if (failed)



}


int handleFileUpload(CacheEntryT *entry,
                     const char * host,
                     const char * path,
                     const int    port,
                     const int    clientSocket,
                     char *       buffer,
                     const size_t bufferSize) {
  const int remoteSocket = getSocketOfRemote(host, port);
  if (remoteSocket < 0) {
    logError("%s, %d failed getSocketOfRemote of host:port %s:%d",
             __FILE__, __LINE__, host, port);
    sendError(clientSocket, BadGatewayStatus, FailedToConnectRemoteServer);
    return ERROR;
  }
  pthread_t    thread;
  UploadArgsT *args = malloc(sizeof(*args));
  if (args == NULL) {
    logError("%s, %d malloc", __FILE__, __LINE__);
    sendError(clientSocket, InternalErrorStatus, "");
    return ERROR;
  }
  const int ret = pthread_create(&thread,NULL, fileUploaderStartup, args);
  if (ret != 0) {
    logError("%s, %d pthread_create", __FILE__, __LINE__);
    sendError(clientSocket, InternalErrorStatus, "");
    return ERROR;
  }
  return SUCCESS;
}
