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
  UploadArgsT *uploadArgs   = args;
  CacheEntryT *entry        = uploadArgs->entry;
  BufferT *    buffer       = uploadArgs->buffer;
  const int    remoteSocket = uploadArgs->remoteSocket;
  int          uploadStatus = SUCCESS;
  while (1) {
    const size_t readed = recvNWithTimeout(
      remoteSocket, buffer->data, buffer->maxSize, SEND_RECV_TIMEOUT
    );
    if (errno != 0) {
      logError("%s:%d recv %s",__FILE__, __LINE__, strerror(errno));
      uploadStatus = ERROR;
      break;
    }
    if (readed == 0) {
      break;
    }

    buffer->occupancy          = readed;
    CacheEntryChunkT *curChunk = CacheEntryT_appendData(
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

  free(uploadArgs);
  return NULL;
}

int handleFileUpload(CacheEntryT *  entry,
                     const BufferT *buffer,
                     const int      clientSocket, const int remoteSocket) {
  pthread_t    thread;
  UploadArgsT *args = NULL;
  args              = malloc(sizeof(*args));
  if (args == NULL) {
    logError("%s, %d malloc", __FILE__, __LINE__);
    goto uploadFailed;
  }
  BufferT *uploadBuffer = BufferT_new(buffer->maxSize);
  if (uploadBuffer == NULL) {
    logError("%s, %d BufferT_new %s", __FILE__, __LINE__, strerror(errno));
    goto uploadFailed;
  }

  args->buffer            = uploadBuffer;
  args->remoteSocket      = remoteSocket;
  args->clientSocket      = clientSocket;
  args->entry             = entry;

  int ret = pthread_create(&thread,NULL, fileUploaderStartup, args);
  if (ret != 0) {
    logError("%s, %d pthread_create", __FILE__, __LINE__);
    goto uploadFailed;
  }

  ret = pthread_detach(thread);
  if (ret < 0) {
    CHECK_RET("pthread_detach", ret);
  }
  return SUCCESS;

uploadFailed:
  BufferT_delete(uploadBuffer);
  sendError(clientSocket, InternalErrorStatus, "");
  free(args);
  return ERROR;
}
