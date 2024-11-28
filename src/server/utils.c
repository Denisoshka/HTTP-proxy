#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "proxy.h"
#include "../utils/log.h"

#define DEF_HTTP_PORT 80

int parseURL(const char *url, char *host, char *path, int *port) {
  *port = DEF_HTTP_PORT;
  strcpy(path, "");
  // http://host:port/path
  if (sscanf(url, "http://%99[^:/]:%d/%1999[^\n]", host, port, path) == 3) {
    // http://host/path
  } else if (sscanf(url, "http://%99[^/]/%1999[^\n]", host, path) == 2) {
    // http://host:port
  } else if (sscanf(url, "http://%99[^:/]:%d[^\n]", host, port) == 2) {
    strcpy(path, "");
    // http://host
  } else if (sscanf(url, "http://%99[^\n]", host) == 1) {
    strcpy(path, "");
  } else {
    return ERROR;
  }

  if (strlen(path) == 0) {
    host[strlen(host) - 1] = '\0';
  }

  return SUCCESS;
}

size_t sendN(const int socket, const char *buffer, const size_t size) {
  size_t totalSent = 0;
  while (totalSent < size) {
    const ssize_t bytesSent = send(
      socket, buffer + totalSent, size - totalSent, 0
    );
    if (bytesSent < 0) {
      return totalSent;
    }
    totalSent += bytesSent;
  }
  return totalSent;
}

/*ssize_t recvN(const int socket, char *buffer, const size_t size) {
  size_t totalReceived = 0;
  while (totalReceived < size) {
    const ssize_t bytesReceived = recv(
      socket, buffer + totalReceived, size - totalReceived, 0
    );
    if (bytesReceived == 0) {
      break;
    }
    if (bytesReceived < 0) {
      return ERROR;
    }

    totalReceived += bytesReceived;
  }
  return totalReceived;
}*/

ssize_t recvNWithTimeout(
  const int socket, char *buffer, const size_t size, const long mstimeout
) {
  size_t totalReceived = 0;
  while (totalReceived < size) {
    const ssize_t bytesReceived = recvWithTimeout(
      socket, buffer + totalReceived, size - totalReceived, mstimeout
    );
    if (bytesReceived == 0 || bytesReceived == RECV_TIMEOUT_EXPIRED) {
      break;
    }
    if (bytesReceived < 0) {
      return ERROR;
    }

    totalReceived += bytesReceived;
  }
  return totalReceived;
}

/**
 *
 * @param socket
 * @param buffer
 * @param size
 * @param mstimeout
 * @return received data size if success, -1 on error, -2 on timeout
 */
ssize_t recvWithTimeout(
  const int socket, char *buffer, const size_t size, const long mstimeout
) {
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(socket, &read_fds);

  struct timeval timeoutSt;
  timeoutSt.tv_sec = mstimeout / 1000;
  timeoutSt.tv_usec = (mstimeout % 1000) * 1000;

  const int ready = select(
    socket + 1, &read_fds, NULL, NULL, &timeoutSt
  );
  if (ready < 0) {
    int savedErrno = errno;
    logError("%s:%d select() failed %s", __FILE__, __LINE__,
             strerror(savedErrno));
    return ERROR;
  }
  if (ready == 0) {
    return RECV_TIMEOUT_EXPIRED;
  }

  // Receive
  const ssize_t receivedBytes = recv(socket, buffer, size, 0);
  if (receivedBytes < 0) {
    logError("%s:%d recv %s",
             __FILE__, __LINE__, strerror(errno));
    return ERROR;
  }
  return receivedBytes;
}

void sendError(const int sock, const char *status, const char *message) {
  constexpr size_t contentLenReserve = 20;
  const char *     responseTemplate = "HTTP/1.1 %s\r\n"
      "C ontent-Type: text/plain\r\n"
      "Content-Length: %zu\r\n"
      "\r\n"
      "%s";
  const size_t responseLength = strlen(responseTemplate)
                                + strlen(message)
                                + strlen(status)
                                + contentLenReserve;
  char *response = malloc(responseLength * sizeof(*response));
  snprintf(
    response,
    responseLength * sizeof(*response),
    responseTemplate,
    status,
    strlen(message),
    message
  );
  sendN(sock, response, strlen(response));
  free(response);
}

/**
 * @param host destination server host
 * @param port destination server port
 * @return server socket
 */
int getSocketOfRemote(const char *host, const int port) {
  const struct hostent *server = gethostbyname(host);
  if (server == NULL) {
    logError("%s : %d gethostbyname %s", __FILE__, __LINE__, strerror(errno));
    return ERROR;
  }

  int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (serverSocket < 0) {
    logError(
      "%s : %d failed to create server socket %s",
      __FILE__, __LINE__, strerror(serverSocket)
    );
    return ERROR;
  }

  struct sockaddr_in server_addr = {0};
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  const int ret = connect(
    serverSocket, (struct sockaddr *) &server_addr, sizeof(server_addr)
  );
  if (ret < 0) {
    logError(
      "%s : %d failed to connect server : %s port : %d, error %s",
      __FILE__, __LINE__, host, port, strerror(ret)
    );
    goto destroySocket;
  }

  return serverSocket;

destroySocket:
  close(serverSocket);
  return ERROR;
}
