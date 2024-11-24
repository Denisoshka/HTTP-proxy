#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "../utils/log.h"

#define BUFFER_SIZE 4096
#define DEF_HTTP_PORT 80
#define ERROR (-1)
#define SUCCESS 0

ssize_t readHttpHeaders(const int    client_socket,
                        char *       buffer,
                        const size_t buffer_size) {
  ssize_t total_read = 0;

  while (total_read < buffer_size - 1) {
    const ssize_t bytes_read = recv(
      client_socket, buffer + total_read, buffer_size - total_read - 1, 0
    );
    if (bytes_read <= 0) {
      return -1;
    }

    total_read += bytes_read;
    buffer[total_read] = '\0';

    if (strstr(buffer, "\r\n\r\n")) {
      return total_read;
    }
  }
  return -1;
}


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

ssize_t sendN(const int socket, const void *buffer, const size_t size) {
  size_t totalSent = 0;
  while (totalSent < size) {
    const ssize_t bytesSent = send(
      socket, buffer + totalSent, size - totalSent, 0
    );
    if (bytesSent < 0) {
      return -1;
    }
    totalSent += bytesSent;
  }
  return totalSent;
}

ssize_t recvN(const int socket, void *buffer, const size_t size) {
  size_t totalReceived = 0;
  while (totalReceived < size) {
    const ssize_t bytesReceived = recv(
      socket, buffer + totalReceived, size - totalReceived, 0
    );
    if (bytesReceived < 0) {
      return ERROR;
    }
    totalReceived += bytesReceived;
  }
  return totalReceived;
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
    sizeof(response),
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

  int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (serverSocket < 0) {
    logError(
      "%s : %d failed to create server socket %s",
      __FILE__, __LINE__, strerror(errno)
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
      __FILE__, __LINE__, host, port, strerror(errno)
    );
    goto destroySocket;
  }

  return serverSocket;

destroySocket:
  close(serverSocket);
  return ERROR;
}
