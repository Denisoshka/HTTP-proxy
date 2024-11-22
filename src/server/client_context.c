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
#define HOST_MAX_LEN 1024
#define PATH_MAX_LEN 2048
const char *badRequestResponse =
    "HTTP/1.1 400 Bad Request\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 15\r\n"
    "\r\n"
    "Invalid URL.\r\n";
const int badRequestResponseLen = sizeof(badRequestResponse);

int getSocketOfRemote(const char *host, int port);

void handleConnection(ClientContextArgsT *args) {
  char protocol[PROTOCOL_MAX_LEN];
  char method[METHOD_MAX_LEN];
  char buffer[BUFFER_SIZE];
  char host[HOST_MAX_LEN] = {0};
  char path[PATH_MAX_LEN] = {0};
  char url[URL_MAX_LEN];

  struct sockaddr_in server_addr;

  const int      clientSocket = args->clientSocket;
  CacheManagerT *cacheManager = args->cacheManager;

  const int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
  if (bytesRead <= 0) {
    logError("failed to read request %s", strerror(errno));
    perror("Failed to read from client");
    close(clientSocket);
    return;
  }

  buffer[bytesRead] = '\0';
  sscanf(buffer, "%s %s %s", method, url, protocol);
  if (strcmp(method, "GET") == 0) {
    int ret = pthread_mutex_lock(&cacheManager->entriesMutex);
    if (ret != 0) {
      logFatal("[handleConnection] pthread_mutex_lock %s", strerror(errno));
      abort();
    }

    CacheNodeT *node = CacheManagerT_get_CacheNodeT(cacheManager, url);
    if (node == NULL) {
      node = CacheNodeT_createFor_CacheManagerT(url);
      if (node == NULL) {
        goto closeClientSocket;
        //todo
      }

      node->entry->


    }
    CacheEntryT_acquire(node->entry);
    if (node->entry->dataChunks == NULL) {
    } else {
    }
    ret = pthread_mutex_unlock(&cacheManager->entriesMutex);
    if (ret != 0) {
      logFatal("[handleConnection] pthread_mutex_unlock %s", strerror(errno));
      abort();
    }


    int port = 0;
    ret = parseURL(url, host, path, &port);
    if (ret != SUCCESS) {
      logError("failed to parse URL %s", url);
      sendN(clientSocket, badRequestResponse, badRequestResponseLen);
      goto closeClientSocket;
    }

    const int remoteSocket = getSocketOfRemote(host, port);
    if (remoteSocket < 0) {
      goto closeClientSocket;
    }
  } else {
  }


closeClientSocket:
  close(clientSocket);
}

/**
 * @param host destination server host
 * @param port destination server port
 * @return server socket
 */
int getSocketOfRemote(const char *host, const int port) {
  struct sockaddr_in    server_addr;
  const struct hostent *server = gethostbyname(host);

  int server_socket = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket < 0) {
    logError("failed to create server socket %s", strerror(errno));
    return ERROR;
  }

  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  const int ret = connect(
    server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)
  );
  if (ret < 0) {
    logError("failed to connect server : %s port : %d, error %s",
             host, port, strerror(errno)
    );
    goto destroySocket;
  }

  return SUCCESS;

destroySocket:
  close(server_socket);
  return ERROR;
}
