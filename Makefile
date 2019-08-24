
CFLAGS += -Wall -g -I/usr/include/fuse3 -lpthread -lfuse3 -D_FILE_OFFSET_BITS=64

all: netfs_client netfs_server

netfs_client: netfs_client.o net.o common.h logging.h
	$(CC) $(CFLAGS) $^ -o $@

netfs_server: netfs_server.o net.o common.h logging.h
	$(CC) $(CFLAGS) $^ -o $@

net.o: net.c net.h logging.h
netfs_client.o: netfs_client.c netfs_client.h common.h logging.h net.o
netfs_server.o: netfs_server.c netfs_server.h common.h logging.h net.o

clean:
	rm -f netfs_client netfs_server net.o netfs_server.o netfs_client.o

