#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../utils/log.h"

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

ssize_t send_all(int socket, const void *buffer, size_t length) {
  size_t  total_sent = 0;
  ssize_t bytes_sent;
  while (total_sent < length) {
    bytes_sent = send(socket, buffer + total_sent, length - total_sent, 0);
    if (bytes_sent == -1) {
      perror("send failed");
      return -1; // Ошибка отправки
    }
    total_sent += bytes_sent;
  }
  return total_sent;
}
