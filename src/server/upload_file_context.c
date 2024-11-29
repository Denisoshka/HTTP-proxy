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

static int readSendRestBytes(
  const int clientSocket,
  const int remoteSocket,
  BufferT * buffer
) {
  sendN(remoteSocket, buffer->data, buffer->occupancy);
  if (errno != 0) {
    logError("%s:%d sendN %s",__FILE__,__LINE__, strerror(errno));
    return ERROR;
  }

  while (1) {
    const ssize_t readed = recvNWithTimeout(
      clientSocket, buffer->data, buffer->maxSize,RECV_TIMEOUT
    );
    if (readed == ERROR) {
      return ERROR;
    }
    if (readed == 0) {
      break;
    }
    sendN(
      remoteSocket, buffer->data, buffer->occupancy
    );
    if (errno != 0) { return ERROR; }
  }
  return SUCCESS;
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

  int ret = readSendRestBytes(
    clientSocket, remoteSocket, buffer
  );
  if (ret < 0) {
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
    CacheEntryChunkT * curChunk = CacheEntryT_appendData(
      entry, buffer->data, buffer->occupancy, InProcess
    );
    if (curChunk == NULL) {
      logError("%s:%d fillCache %s",__FILE__, __LINE__, strerror(errno));
      uploadStatus = ERROR;
      break;
    }
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


int handleFileUpload(CacheEntryT *  entry,
                     const BufferT *buffer,
                     const char *   host,
                     const int      port,
                     const int      clientSocket) {
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
  BufferT *copyOfBuffer = BufferT_new(buffer->maxSize);
  if (copyOfBuffer == NULL) {
    logError("%s, %d BufferT_new %s", __FILE__, __LINE__, strerror(errno));
    goto uploadFailed;
  }

  memcpy(copyOfBuffer->data, buffer->data, buffer->maxSize);
  args->buffer = copyOfBuffer;
  args->buffer->occupancy = buffer->occupancy;
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
