/**
 * netfs_client.h
 *
 * Implementation of the netfs client file system. Based on the fuse 'hello'
 * example here: https://github.com/libfuse/libfuse/blob/master/example/hello.c
 */

#define FUSE_USE_VERSION 31

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <netdb.h> 
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <unistd.h>

#include "common.h"
#include "logging.h"
#include "net.h"
#include "netfs_client.h"

/* This sends the meta data about what we need sent back from the server */
int send_path(int type, int server_fd, const char* path) {
    // setting up metadata to be sent
    struct netfs_msg_header req_header = { 0 };
    req_header.msg_type = type;
    req_header.msg_len = strlen(path) + 1;

    // send meta data
    if (write_len(server_fd, &req_header, sizeof(struct netfs_msg_header)) == -1 ||
                    write_len(server_fd, path, req_header.msg_len) == -1) {
        perror("write_len");
        close(server_fd);
        return -1;
    }

    return 0;
}

/* Command line options */
static struct options {
    int show_help;
    int port;
    char *server;
} options;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }

/* Command line option specification. We can add more here. If we're interested
 * in a string, specify --opt=%s .*/
static const struct fuse_opt option_spec[] = {
    OPTION("-h", show_help),
    OPTION("--help", show_help),
    OPTION("--port=%d", port),
    OPTION("--server=%s", server),
    FUSE_OPT_END
};

static int netfs_getattr(
        const char *path, struct stat *stbuf, struct fuse_file_info *fi) {

    LOG("getattr: %s\n", path);

    /* Clear the stat buffer */
    memset(stbuf, 0, sizeof(struct stat));

    /* By default, we will return 0 from this function (success) */
    int res = 0;

    // connect_to the given server
    int server_fd = connect_to(options.server, options.port);
    if (server_fd == -1) {
        perror("connect_to");
        return -1;
    }
    // send serverr metadata
    if (send_path(MSG_GETATTR, server_fd, path) == -1) {
        close(server_fd);
        return -1;
    }

    /* this automatically sets the stat struct to what it needs to be, however,
     * the original directory that is passed, the permissions are a little
     * messed up therefore I have it set to owner RWX and group and other to RX
     */
    if (read_len(server_fd, stbuf, sizeof(struct stat)) == -1) {
        perror("read_len");
        close(server_fd);
        return -1;
    }

    /* Sets the permissions to drwxr-xr-r */
    if (strcmp(path, "/") == 0) {
        /* This is the root directory. We have hard-coded the permissions to 755
         * here, but you should apply the permissions from the remote directory
         * instead. The mode means:
         *   - S_IFDIR: this is a directory
         *   - 0755: user can read, write, execute. All others can read+execute.
         * The number of links refers to how many hard links point to the file.
         * If the link count reaches 0, the file is effectively deleted (this is
         * why deleting a file is actually 'unlinking' it).
         */
        stbuf->st_mode = S_IFDIR | 0755;
    } 
    
    return res;
}

static int netfs_readdir(
        const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
        struct fuse_file_info *fi, enum fuse_readdir_flags flags) {

    LOG("readdir: %s\n", path);

    /* By default, we will return 0 from this function (success) */
    int res = 0;

    // connect_to the given server
    int server_fd = connect_to(options.server, options.port);
    if (server_fd == -1) {
        perror("connect_to");
        return -1;
    }

    // send serverr metadata
    if (send_path(MSG_READDIR, server_fd, path) == -1) {
        close(server_fd);
        return -1;
    }

    // gets the responses from the server
    uint16_t reply_len;
    char reply[LARGEST_PATH] = { 0 };
    do {
        // get length
        if (read_len(server_fd, &reply_len, sizeof(uint16_t)) == -1) {
            perror("read_len");
            close(server_fd);
            return -1;
        }
        //get actual name
        if (read_len(server_fd, reply, reply_len) == -1) {
            perror("read_len");
            close(server_fd);
            return -1;
        }
        filler(buf, reply, NULL, 0, 0);
    } while (reply_len > 0);

    close(server_fd);

    return res;
}

static int netfs_open(const char *path, struct fuse_file_info *fi) {

    LOG("open: %s\n", path);

    /* By default, we will return 0 from this function (success) */
    int res = 0;

    /* We only support opening the file in read-only mode */
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EACCES;
    }

    // connect to the given server
    int server_fd = connect_to(options.server, options.port);
    if (server_fd == -1) {
        perror("connect_to");
        return -1;
    }

    // send meta data to server
    if (send_path(MSG_OPEN, server_fd, path) == -1) {
        close(server_fd);
        return -1;
    }

    // handle response from server
    uint16_t response;
    if (read_len(server_fd, &response, sizeof(uint16_t)) == -1) {
        perror("read_len");
        close(server_fd);
        return -1;
    }

    // if response is not 1 then it failed, update res to -1
    if (response != 1) {
        res = -1;
    }

    close(server_fd);    

    return res;
}

static int netfs_read(
        const char *path, char *buf, size_t size, off_t offset,
        struct fuse_file_info *fi) {

    LOG("read: %s\n", path);

    // connect to the given server
    int server_fd = connect_to(options.server, options.port);
    if (server_fd == -1) {
        perror("connect_to");
        return -1;
    }

    // send meta data to server
    if (send_path(MSG_READ, server_fd, path) == -1) {
        close(server_fd);
        return -1;
    }

    // sends the offset
    if (write_len(server_fd, &offset, sizeof(off_t)) == -1) {
        perror("write_len");
        close(server_fd);
        return -1;
    }

    // sends the size
    if (write_len(server_fd, &size, sizeof(size_t)) == -1) {
        perror("write_len");
        close(server_fd);
        return -1;
    }

    // reads the len of the data we are getting
    off_t len;
    if (read_len(server_fd, &len, sizeof(off_t)) == -1) {
        perror("read_len");
        close(server_fd);
        return -1;
    }
    // reads in the file contents
    if (read_len(server_fd, buf, len) == -1) {
        perror("read_len");
        close(server_fd);
        return -1;
    }

    close(server_fd);

    return size;
}

/* This struct maps file system operations to our custom functions defined
 * above. */
static struct fuse_operations netfs_client_ops = {
    .getattr = netfs_getattr,
    .readdir = netfs_readdir,
    .open = netfs_open,
    .read = netfs_read,
};

static void show_help(char *argv[]) {
    printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
    printf("File-system specific options:\n"
            "    --port=<n>          Port number to connect to\n"
            "                        (default: %d)\n"
            "    --server=<n>        Hostname or IP Address to connect to\n"
            "    -f                  Foreground option, helpful for debugging."
            "\n", DEFAULT_PORT);
}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        show_help(argv);
        return 1;
    }

    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    /* Set up default options: */
    options.port = DEFAULT_PORT;

    /* Parse options */
    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
        return 1;
    }

    if (options.show_help) {
        show_help(argv);
        assert(fuse_opt_add_arg(&args, "--help") == 0);
        args.argv[0] = (char*) "";
    }

    return fuse_main(args.argc, args.argv, &netfs_client_ops, NULL);
}
