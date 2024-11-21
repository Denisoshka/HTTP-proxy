#include <errno.h>

#include "proxy.h"
#include "log.h"

#include <bits/pthreadtypes.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

#define SERVER_BACKLOG 10

typedef struct {
  int clientSocket;
} ClientThreadRoutineArgs;

void setupSigPipeIgnore(void) {
  struct sigaction sa;
  sa.sa_handler = SIG_IGN; // Устанавливаем обработчик для SIGPIPE
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

void *clientThreadRoutine(void *args) {
  int clientSocket = ((ClientThreadRoutineArgs *) args)->clientSocket;

  handleRequest(clientSocket);
  free(args);
  close(clientSocket);
  return NULL;
}

void startServer(const int port) {
  setupSigPipeIgnore();

  struct sockaddr_in serverAddr, clientAddr;
  socklen_t          clientAddrLen = sizeof(clientAddr);

  int serverSocket = setupServerSocket(&serverAddr, port);
  int ret = bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof
                 (serverAddr));
  if (ret < 0) {
    perror("bind failed");
    abort();
  }
  ret = listen(serverSocket, SERVER_BACKLOG);
  if (ret < 0) {
    perror("Listen error");
    abort();
  }
  logInfo("proxy start on %d port", port);

  logInfo("wait connections");

  while (1) {
    const int clientSocket = accept(
      serverSocket, (struct sockaddr *) &clientAddr, &clientAddrLen
    );
    char addrBuf[sizeof(struct in_addr)];
    inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, addrBuf, clientAddrLen);

    if (clientSocket < 0) {
      logInfo("error while accepting connection: %s ", strerror(errno));
      continue;
    }
    logInfo(
      "server accepted connection: %s:%d", addrBuf, ntohs(clientAddr.sin_port)
    );

    ClientThreadRoutineArgs *args = malloc(sizeof(ClientThreadRoutineArgs));
    if (args == NULL) {
      logError("ClientThreadRoutineArgs malloc error: %s", strerror(errno));
      abort();
    }

    args->clientSocket = clientSocket;
    pthread_t clientThreadId;
    pthread_create(&clientThreadId, NULL, clientThreadRoutine, args);
    pthread_detach(clientThreadId);
  }
}
