#ifndef _NET_H_
#define _NET_H_

#include <unistd.h>
#include <stdint.h>

enum msg_types {
	MSG_READDIR = 1,
	MSG_GETATTR = 2,
	MSG_OPEN = 3,
	MSG_READ = 4,
};

struct __attribute__((__packed__)) netfs_msg_header {
    uint64_t msg_len;
    uint16_t msg_type;
};

ssize_t write_len(int fd, const void *buf, size_t length);
ssize_t read_len(int fd, void *buf, size_t length);
int connect_to(char* hostname, int port);


#endif