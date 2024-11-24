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
#define ERROR 1
#define HOST_MAX_LEN 1024
#define PATH_MAX_LEN 2048

static constexpr size_t kDefCacheChunkSize = 1024*1024;

typedef struct Buffer {
  char *  data;
  ssize_t occupancy;
  ssize_t maxSize;
} BufferT;

typedef struct ClientContextArgs {
  CacheManagerT *cacheManager;
  int            clientSocket;
} ClientContextArgsT;

typedef struct FileUploadContextArgs {
  CacheEntryT *cacheEntry;
  int          clientSocket;
  int          port = 0;
} FileUploadContextArgsT;

const char *badRequestResponse =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 15\r\n"
    "\r\n"
    "Invalid URL.\r\n";
const char *BadRequestStatus = "400 Bad Request";
const char *InternalErrorStatus = "500 Internal Server Error";
const char *BadGatewayStatus = "502 Bad Gateway";
const char *BadRequestMessage = "Failed to read request";
const char *InvalidRequestMessage = "Invalid HTTP request format";
const char *FailedToConnectRemoteServer =
    "Failed to connect to destination server";

BufferT *BufferT_new(size_t maxOccupancy);

void BufferT_delete(BufferT *buffer);

ClientContextArgsT *ClientContextArgsT_create();

void ContextArgsT_delete(ClientContextArgsT *clientContextArgs);

ssize_t readHttpHeaders(int client_socket, char *buffer, size_t buffer_size);

void sendError(int sock, const char *status, const char *message);

ssize_t sendN(int socket, const void *buffer, size_t size);

ssize_t recvN(int socket, void *buffer, size_t size);

int parseURL(const char *url, char *host, char *path, int *port);

int getSocketOfRemote(const char *host, int port);

void handleRequest(int clientSocket);

int handleFileUpload(CacheEntryT *entry, int clientSocket);

void *downloadData(void *args);

void startServer(int port);

#endif //PROXY_H
