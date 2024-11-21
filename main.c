#include <stdio.h>
#include <stdlib.h>

#include "src/log.h"
#include "src/proxy.h"

#define ERROR -1;
#define SUCCESS 0

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    return ERROR;
  }

	const int port = atoi(argv[1]);
  if (port <= 0 || port > 65535) {
    fprintf(stderr, "Invalid port number: %s\n", argv[1]);
    return ERROR;
  }
  startServer(port);

  return 0;
}
