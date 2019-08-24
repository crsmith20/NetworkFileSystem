#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>

#include "net.h"
#include "logging.h"

ssize_t write_len(int fd, const void *buf, size_t length) {
    ssize_t bytes = 0;
    size_t bytes_written = 0;
    while (bytes_written < length) {
        bytes = write(fd, buf + bytes_written, length - bytes_written);
        if (bytes == -1) {
            perror("write");
            return -1;
        }
        bytes_written += bytes;
    }
    LOG("Wrote %zu bytes.\n", bytes_written);
    return bytes_written;
}

ssize_t read_len(int fd, void  *buf, size_t length) {
    ssize_t bytes = 0;
    size_t bytes_read = 0;
    while (bytes_read < length) {
        bytes = read(fd, buf + bytes_read, length - bytes_read);
        if (bytes == -1) {
            perror("read");
            return -1;
        }
        bytes_read += bytes;
    }
    LOG("Read %zu bytes.\n", bytes_read);
    return bytes_read;
}

int connect_to(char* hostname, int port) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        return -1;
    }

    struct hostent *server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "Could not resolve host: %s\n", hostname);
        return -1;
    }

    struct sockaddr_in serv_addr = { 0 };
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = *((struct in_addr *) server->h_addr);

    if (connect(
                socket_fd,
                (struct sockaddr *) &serv_addr,
                sizeof(struct sockaddr_in)) == -1) {

        perror("connect");
        return -1;
    }

    return socket_fd;
}