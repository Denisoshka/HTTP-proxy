#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "proxy.h"
#include "../utils/log.h"

#define ERROR (-1)
#define SUCCESS 0

#define BUFFER_SIZE 8192
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

void handleConnection(ClientContextArgsT *args) {
  char protocol[PROTOCOL_MAX_LEN];
  char method[METHOD_MAX_LEN];
  char buffer[BUFFER_SIZE];
  char host[HOST_MAX_LEN] = {0};
  char path[PATH_MAX_LEN] = {0};
  char url[URL_MAX_LEN];

  struct sockaddr_in server_addr;
  struct hostent *   server;

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
    // todo get cache entry
  }
  int port = 0;
  int ret = parseURL(url, host, path, &port);
  if (ret != SUCCESS) {
    logError("failed to parse URL %s", url);
  }

  while()
}
