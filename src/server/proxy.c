#include "proxy.h"

#include <errno.h>

#include "proxy.h"
#include "../utils/log.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "../cache/cache.h"

#define SERVER_BACKLOG 10

const char *BadRequestStatus =
    "400 Bad Request";
const char *InternalErrorStatus =
    "500 Internal Server Error";
const char *BadGatewayStatus =
    "502 Bad Gateway";
const char *BadRequestMessage =
    "Failed to read request";
const char *InvalidRequestMessage =
    "Invalid HTTP request format";
const char *FailedToConnectRemoteServer =
    "Failed to connect to destination server";

void setupSigPipeIgnore(void) {
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPIPE, &sa, NULL);
}

int setupServerSocket(struct sockaddr_in *serverAddr, const int port) {
  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    perror("create socket failure");
    abort();
  }

  serverAddr->sin_family = AF_INET;
  serverAddr->sin_addr.s_addr = INADDR_ANY;
  serverAddr->sin_port = htons(port);

  int opt = 1;
  int ret = setsockopt(
    serverSocket,
    SOL_SOCKET,
    SO_REUSEPORT,
    &opt,
    sizeof(opt)
  );
  if (ret < 0) {
    perror("setsockopt(SO_REUSEPORT) failed");
    abort();
  }
  ret = setsockopt(
    serverSocket,SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)
  );
  if (ret < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
    abort();
  }
  return serverSocket;
}

void startServer(const int port) {
  setupSigPipeIgnore();
  struct sockaddr_in serverAddr;
  struct sockaddr_in clientAddr;
  socklen_t          clientAddrLen = sizeof(clientAddr);

  int serverSocket = setupServerSocket(&serverAddr, port);
  int ret = bind(
    serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)
  );
  if (ret < 0) {
    logError("server bind failed %s", strerror(errno));
    perror("bind failed");
    abort();
  }

  ret = listen(serverSocket, SERVER_BACKLOG);
  if (ret < 0) {
    logError("[startServer] %s", strerror(errno));
    abort();
  }
  CacheManagerT *cacheManager = CacheManagerT_new();
  logInfo("proxy start on %d port", port);
  logInfo("wait connections");

  while (1) {
    const int clientSocket = accept(
      serverSocket, (struct sockaddr *) &clientAddr, &clientAddrLen
    );
    char addrBuf[sizeof(struct in_addr)];
    inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, addrBuf, clientAddrLen);

    if (clientSocket < 0) {
      logError("error while accepting connection: %s ", strerror(errno));
      continue;
    }
    logInfo(
      "server accepted connection: %s:%d", addrBuf, ntohs(clientAddr.sin_port)
    );

    ClientContextArgsT *args = malloc(sizeof(*args));
    if (args == NULL) {
      logError("ClientThreadRoutineArgs malloc error: %s", strerror(errno));
      sendError(clientSocket, InternalErrorStatus, "");
      close(clientSocket);
      continue;
    }
    args->cacheManager = cacheManager;
    args->clientSocket = clientSocket;
    pthread_t clientThread;
    ret = pthread_create(&clientThread, NULL, clientConnectionHandler, args);
    if (ret < 0) {
      logError("unable to create client thread: %s", strerror(errno));
      sendError(clientSocket, InternalErrorStatus, "");
      free(args);
      continue;
    }
    pthread_detach(clientThread);
  }
}
