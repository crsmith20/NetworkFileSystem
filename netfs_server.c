/**
 * netfs_server.h
 *
 * NetFS file server implementation.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <dirent.h>
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
#include <sys/stat.h>
#include <sys/sendfile.h>


#include "common.h"
#include "logging.h"
#include "net.h"
#include "netfs_server.h"

void handle_request(int fd) {
    // this will read in the metadata of the communication
    struct netfs_msg_header req_header = { 0 };
    if (read_len(fd, &req_header, sizeof(struct netfs_msg_header)) == -1) { // first read_len
        perror("read_len");
        return;
    }

    LOG("Handling Request: [type: %d; length:%lld]\n",
        req_header.msg_type,
        req_header.msg_len);

    // this reads in the path
    char path[LARGEST_PATH] = { 0 };
    if (read_len(fd, path, req_header.msg_len) == -1) { // second read_len
        perror("read_len");
        return;
    }


    // this creates a full path of the location that the client is viewing
    char full_path[LARGEST_PATH] = { 0 };
    strcpy(full_path, ".");
    strcat(full_path, path);
    uint16_t type = req_header.msg_type;

    // handles the request based on the request type
    if (type == MSG_READDIR) {

        LOG("readdir: %s\n", full_path);

        // gets the given directory
        DIR *directory;
        if ((directory = opendir(full_path)) == NULL) {
            perror("opendir");
            close(fd);
            return;
        }
        // writes writes all the given entries
        uint16_t len;
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            len = strlen(entry->d_name) + 1;
            if (write_len(fd, &len, sizeof(uint16_t)) == -1 || 
                        write_len(fd, entry->d_name, len) == -1) {
                perror("write_len");
                close(fd);
                return;
            }
        }
        // writes one more time to signal nothing left to write
        len = 0;
        if (write_len(fd, &len, sizeof(uint16_t)) == -1) {
            perror("write_len");
            close(fd);
            return;
        }

        // close directory and socket connection
        closedir(directory);
        close(fd);

        return;
    } else if (type == MSG_GETATTR) {

        LOG("getattr: %s\n", full_path);

        // gets the stat info for the given path
        struct stat statbuf = { 0 };
        if (stat(full_path, &statbuf) == -1) {
                perror("stat");
                close(fd);
                return;
        }

        // writes the stat info to the client side
        if (write_len(fd, &statbuf, sizeof(struct stat)) == -1) {
            perror("write_len");
            close(fd);
            return;
        }

        close(fd);

        return;
    } else if (type == MSG_OPEN) {

        LOG("open: %s\n", full_path);

        int new;
        new = open(full_path, O_RDWR);
        uint16_t response;
        if (new == -1) {
            response = 0;
            perror("open");
        } else {
            response = 1;
        }

        if (write_len(fd, &response, sizeof(uint16_t)) == -1) {
            perror("write_len");
            close(fd);
            return;
        }
        close(fd);

        return;
    } else if (type == MSG_READ) {

        LOG("read: %s\n", full_path);

        // receives the offset
        off_t offset;
        if (read_len(fd, &offset, sizeof(off_t)) == -1) {
            perror("read_len");
            close(fd);
            return;
        }

        // receives the size
        size_t size;
        if (read_len(fd, &size, sizeof(size_t)) == -1) {
            perror("read_len");
            close(fd);
            return;
        }

        // opens the file
        int read_fd = open(full_path, O_RDWR);
        if (read_fd == -1) {
            perror("open");
            close(fd);
            return;
        }

        // creates a stat struct to get length of the file to send it back
        struct stat statbuf = { 0 };
        if (stat(full_path, &statbuf) == -1) {
                perror("stat");
                close(fd);
                return;
        }

        // this writes the size of the file so we know how much is being sent
        if (write_len(fd, &statbuf.st_size - offset, sizeof(off_t)) == -1) {
            perror("write_len");
            close(fd);
            return;
        }

        // sends the files
        ssize_t bytes = 0;
        bytes = sendfile(fd, read_fd, &offset, size);
        if (bytes == -1) {
            perror("sendfile");
            return;
        }

        LOG("Sent file: %s ( %zu bytes ).\n", full_path, bytes);

        close(fd);

        return;
    } else {
        LOG("ERROR: Unknown Request type: %d\n", type);
    }
}

void show_help(char *argv[]) {
    printf("usage: %s <directory> <port>(optional)\t(Default port: %d)\n", argv[0], DEFAULT_PORT);

}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        show_help(argv);
        return 1;
    }

    /* This starter code will initialize the server port and wait to receive a
     * message. */

    int result = chdir(argv[1]);
    if (result == -1) {
        perror("chdir");
        return 1;
    }
    int port = argc == 3 ? atoi(argv[2]) : DEFAULT_PORT;
    if (port < 1024) {
        LOG("Cannot connect to port %d using default port %d instead.\n", port, DEFAULT_PORT);
        port = DEFAULT_PORT;
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    if (listen(socket_fd, 10) == -1) {
        perror("listen");
        return 1;
    }

    LOG("Listening on port %d\n", port);

    // continue indefinitely
    while (true) {

        struct sockaddr_storage client_addr = { 0 };
        socklen_t slen = sizeof(client_addr);

        int client_fd = accept(
                socket_fd,
                (struct sockaddr *) &client_addr,
                &slen);

        if (client_fd == -1) {
            perror("accept");
            return 1;
        }

        char remote_host[INET_ADDRSTRLEN];
        inet_ntop(
                client_addr.ss_family,
                (void *) &(((struct sockaddr_in *) &client_addr)->sin_addr),
                remote_host,
                sizeof(remote_host));
        LOG("Accepted connection from %s\n", remote_host);

        // to handle multiple requests
        pid_t pid = fork();
        if (pid == 0) {
            // child process
            handle_request(client_fd);
            return 0;
        }
    }

    return 0; 
}
