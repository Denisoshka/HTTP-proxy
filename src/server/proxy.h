#ifndef PROXY_H
#define PROXY_H

#include <pthread.h>
#include <stdio.h>

#include "../cache/cache.h"

#define CACHE_SIZE_LIMIT 1048576 //1Mb

/**
 *Should be >= 16kB to fully fit HTTP headers in single buffer
 */
#define BUFFER_SIZE      16384  //16kB 
#define SUCCESS 0
#define ERROR (-1)
#define TIMEOUT_EXPIRED (-2)
#define HOST_MAX_LEN 1024
#define PATH_MAX_LEN 2048
#define SEND_RECV_TIMEOUT 3000

#define CHECK_ERROR(description, ret) \
  do { \
    if (ret != 0) { \
      logFatal( "%s:%d %s : %s", __FILE__, __LINE__, description, strerror(ret) ); \
      abort(); \
    } \
  } while (0)

static constexpr size_t kDefCacheChunkSize = 1024 * 1024;

typedef struct ClientArgs {
  int clientSocket;
} ClientArgsT;

typedef struct Buffer {
  char * data;
  size_t occupancy;
  size_t maxSize;
} BufferT;

typedef struct ClientContextArgs {
  CacheManagerT *cacheManager;
  int            clientSocket;
} ClientContextArgsT;

typedef struct FileUploadContextArgs {
  CacheEntryT *cacheEntry;
  int          clientSocket;
  int          port;
} FileUploadContextArgsT;

extern const char *BadRequestStatus;
extern const char *InternalErrorStatus;
extern const char *BadGatewayStatus;
extern const char *BadRequestMessage;
extern const char *InvalidRequestMessage;
extern const char *FailedToConnectRemoteServer;

BufferT *BufferT_new(size_t maxOccupancy);

void BufferT_delete(BufferT *buffer);

ClientContextArgsT *ClientContextArgsT_create();

void ContextArgsT_delete(ClientContextArgsT *clientContextArgs);

ssize_t readHttpHeaders(int client_socket, char *buffer, size_t buffer_size);

void sendError(int sock, const char *status, const char *message);

size_t sendN(int socket, const char *buffer, size_t size);

// ssize_t recvN(int socket, void *buffer, size_t size);

ssize_t recvWithTimeout(int socket, char *buffer, size_t size, long mstimeout);

size_t recvNWithTimeout(int socket, char *buffer, size_t size, long mstimeout);

/**
 * @param socket
 * @param buffer
 * @param size
 * @param mstimeout
 * @return sent data size if success, -1 on error, -2 on timeout
 */
ssize_t sendWithTimeout(
  int socket, const char *buffer, size_t size, long mstimeout
);

size_t sendNWithTimeout(
  int socket, const char *buffer, size_t size, long mstimeout
);

int parseURL(const char *url, char *host, char *path, int *port);

int getSocketOfRemote(const char *host, int port);

int forwardDataWithTimeout(
  int clientSocket, int remoteSocket, long timeout, BufferT *buffer
);

int handleFileUpload(
  CacheEntryT *  entry,
  const BufferT *buffer,
  int            clientSocket, int remoteSocket
);

void *downloadData(void *args);

void startServer(int port);

void *clientConnectionHandler(void *args);

#endif //PROXY_H
