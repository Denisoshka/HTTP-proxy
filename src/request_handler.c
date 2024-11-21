#include <errno.h>

#include "proxy.h"
#include "log.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <asm-generic/errno-base.h>
#include <sys/socket.h>

#define HTTP_PORT 80
#define EROR_TTL 1
#define ERROR  (-1)

// const char *responseBadRequest = "HTTP/1.0 400 Bad Request\r\n\r\n";
// const char *responseMethodNotAllowed = "HTTP/1.0 405 Method Not Allowed\r\n\r\n";
const char *unsupportedProtocolResponseMsg =
    "HTTP/1.1 505 HTTP Version Not Supported\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 40\r\n"
    "\r\n"
    "Only HTTP protocol is supported.\r\n";
const ssize_t unsupportedProtocolResponseMsgLen = strlen(response);

void sendUnsupportedProtocolResponse(int client_socket) {
  ssize_t sended = 0;
  while (sended < unsupportedProtocolResponseMsgLen) {
    ssize_t ret = send(client_socket, unsupportedProtocolResponseMsg,
                       unsupportedProtocolResponseMsgLen, 0);
    if (ret < 0) {break;}
    sended += ret;
  }
}

static int parseURL(const char *url, char *host, char *path, int *port) {
  char *urlCopy = strdup(url);
  if (!urlCopy) {
    return -1;
  }

  *port = HTTP_PORT;
  // http://host:port/path
  if (sscanf(urlCopy, "http://%99[^:/]:%d/%1999[^\n]", host, port, path) == 3) {
    // http://host/path
  } else if (sscanf(urlCopy, "http://%99[^/]/%1999[^\n]", host, path) == 2) {
    // http://host:port
  } else if (sscanf(urlCopy, "http://%99[^:/]:%d[^\n]", host, port) == 2) {
    strcpy(path, "");
    // http://host
  } else if (sscanf(urlCopy, "http://%99[^\n]", host) == 1) {
    strcpy(path, "");
  } else {
    free(urlCopy);
    return ERROR;
  }

  if (strlen(path) == 0) {
    host[strlen(host) - 1] = '\0';
  }

  free(urlCopy);
  return 0;
}

static int connectToHost(const char *host, int port) {
  struct addrinfo hints, *res, *p;
  char            portStr[6];
  int             sockFd;

  snprintf(portStr, sizeof(portStr), "%d", port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (getaddrinfo(host, portStr, &hints, &res) != 0) {
    return -1;
  }

  for (p = res; p != NULL; p = p->ai_next) {
    if ((sockFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if (connect(sockFd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockFd);
      continue;
    }
    break;
  }

  freeaddrinfo(res);
  return (p == NULL) ? -1 : sockFd;
}

int getHeadersSize(char *buffer) {
  char *headersEnd = strstr(buffer, "\r\n\r\n");
  return headersEnd - buffer;
}

int isResponseStatusOk(char *buffer) {
  return strstr(buffer, "HTTP/1.0 200 OK") || strstr(buffer, "HTTP/1.1 200 OK");
}

typedef struct {
  CacheEntryT *entry;
  char         host[256];
  char         path[1024];
  int          port;
} DownloadDataArgs;

void *downloadData(void *args) {
  CacheEntryT *entry = ((DownloadDataArgs *) args)->entry;
  const char * host = ((DownloadDataArgs *) args)->host;
  const char * path = ((DownloadDataArgs *) args)->path;
  const int    port = ((DownloadDataArgs *) args)->port;

  const int serverSocket = connectToHost(host, port);
  if (serverSocket < 0) {
    logInfo("connection refused %s:%d", host, htons(port));
    cacheMarkComplete(entry);
    deleteEntry(entry);
    return NULL;
  }
  logInfo("connected to: %s:%d/%s", host, port, path);

  char request[BUFFER_SIZE];
  snprintf(request, sizeof(request), "GET /%s HTTP/1.0\r\nHost: %s\r\n\r\n",
           path, host);
  send(serverSocket, request, strlen(request), 0);

  char    buffer[BUFFER_SIZE];
  ssize_t bytesDownload = 0;

  //Receive Headers
  ssize_t bytesReceived = recv(serverSocket, buffer, sizeof(buffer), 0);
  if (bytesReceived <= 0) {
    logInfo("Failed to receive data");
    close(serverSocket);
    cacheMarkComplete(entry);
    cacheMarkOk(entry, 0);
    return NULL;
  }

  if (!isResponseStatusOk(buffer)) {
    logError("Received non 200 Status Code");
    cacheInsertData(entry, buffer, bytesReceived);
    cacheMarkComplete(entry);
    cacheMarkOk(entry, 0);
    return NULL;
  }

  const int headersSize = getHeadersSize(buffer);
  cacheInsertData(entry, buffer + headersSize, bytesReceived - headersSize);
  bytesDownload += bytesReceived - headersSize;

  while ((bytesReceived = recv(serverSocket, buffer, sizeof(buffer), 0)) > 0) {
    cacheInsertData(entry, buffer, bytesReceived);
    bytesDownload += bytesReceived;
  }

  logInfo("Downloaded %zd bytes", bytesDownload);

  cacheMarkComplete(entry);
  close(serverSocket);
  free(args);
  return NULL;
}

void handleRequest(int clientSocket) {
  char buffer[BUFFER_SIZE];
  char method[16], url[URL_MAX_LENGHT], protocol[16];
  char host[256],  path[1024];
  int  port;

  if (recv(clientSocket, buffer, sizeof(buffer) - 1, 0) <= 0) {
    return;
  }

  buffer[BUFFER_SIZE - 1] = '\0';
  sscanf(buffer, "%s %s %s", method, url, protocol);
  if (strncmp(protocol, "HTTP/", 5) != 0) {
    printf("Unsupported protocol: %s\n", protocol);

    // Отправляем клиенту сообщение об ошибке
    sendUnsupportedProtocolResponse(client_socket);
    return;
  }

  /*if (strcmp(method, "GET") != 0) {
    send(clientSocket,
         responseMethodNotAllowed,
         strlen(responseMethodNotAllowed),
         0
    );
    return;
  }*/

  if (parseURL(url, host, path, &port) < 0) {
    logInfo("Host: %s; Port: %d Path: %s", host, port, path);
    send(clientSocket, responseBadRequest, strlen(responseBadRequest), 0);
    return;
  }

  CacheEntryT *entry = getOrCreateCacheEntry(url);
  pthread_mutex_lock(&entry->dataMutex);
  if (!entry->dataAvalible) {
    logInfo("cache miss %s", url);
    DownloadDataArgs *args = malloc(sizeof(DownloadDataArgs));
    if (args == NULL) {
      perror("Malloc error");
      return;
    }

    args->entry = entry;
    strncpy(args->path, path, sizeof(args->path));
    strncpy(args->host, host, sizeof(args->host));
    args->port = port;
    entry->dataAvalible = 1;
    logInfo("start download data %s", url);
    pthread_create(&entry->downloadThread, NULL, downloadData, args);
  } else {
    logInfo("cache hit %s", url);
  }
  pthread_mutex_unlock(&entry->dataMutex);
  //Send to client
  size_t sentBytes = 0;
  while (entry->isOk && !entry->downloadFinished) {
    pthread_mutex_lock(&entry->dataMutex);
    pthread_cond_wait(&entry->dataAvailable, &entry->dataMutex);
    pthread_mutex_unlock(&entry->dataMutex);
  }
  while (entry->isOk && sentBytes < entry->downloadedSize) {
    while (sentBytes < entry->downloadedSize) {
      const size_t chunk = send(
        clientSocket,
        entry->data + sentBytes,
        entry->downloadedSize - sentBytes,
        0
      );
      if (chunk <= 0) {
        if (errno == EPIPE) {
          logError("client %s:%d disconnected", host, htons(port));
        }
        break;
      }
      sentBytes += chunk;
    }
    if (entry->isOk) {
      while (entry->isOk) {
        pthread_mutex_lock(&entry->dataMutex);
        pthread_cond_wait(&entry->dataAvailable, &entry->dataMutex);
        pthread_mutex_unlock(&entry->dataMutex);
      }
    } else {
      break;
    }
  }

  if (!entry->isOk) {
    deleteEntry(entry);
  }

  pthread_mutex_unlock(&entry->dataMutex);
}
