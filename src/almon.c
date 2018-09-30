/** @file       almon.c
 *  @brief      desc.
 *
 *  @author     t-kenji <protect.2501@gmail.com>
 *  @date       2018-09-24 create new.
 *  @copyright  Copyright © 2018 t-kenji
 *
 *  This code is licensed under MIT License.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for strdup() */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

static volatile sig_atomic_t is_running = 1;

#define DEBUG(format, ...)                                    \
    do {                                                      \
        fprintf(stderr, "%s:%d:%s$ " format "\n",             \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

#define lengthof(array) (sizeof(array)/sizeof((array)[0]))

void sig_receive(int signum)
{
    is_running = 0;
}

void usage(const char *name)
{
    printf("usage: %s <stdio-uri>\n", name);
}

int main(int argc, char **argv)
{
    enum {
        ARG_SELF,
        ARG_URI,
        ARG_LENGTH
    };

    signal(SIGINT, sig_receive);

    if (argc < ARG_LENGTH) {
        usage(argv[ARG_SELF]);
        exit(1);
    }

    int stdio_fd;
    char *uri = strdup(argv[ARG_URI]);
    char *proto = uri;
    char *path = strstr(uri, "://");
    if (path == NULL) {
        free(uri);
        usage(argv[ARG_SELF]);
        exit(1);
    }
    *path = '\0';
    path += 3;
    if (strcmp(proto, "fifo") == 0) {
        stdio_fd = open(path, O_RDWR);
        if (stdio_fd < 0) {
            free(uri);
            usage(argv[ARG_SELF]);
            exit(1);
        }
    } else if (strcmp(proto, "pty") == 0) {
        stdio_fd = open(path, O_RDWR);
        if (stdio_fd < 0) {
            free(uri);
            usage(argv[ARG_SELF]);
            exit(1);
        }
    } else {
        free(uri);
        usage(argv[ARG_SELF]);
        exit(1);
    }
    free(uri);

        struct termios saved_term, term;
        tcgetattr(STDIN_FILENO, &saved_term);
        term = saved_term;
        term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        term.c_cflag &= ~(CSIZE | PARENB);
        term.c_cflag |= CS8;
        term.c_oflag &= ~(OPOST);
        term.c_cc[VMIN] = 1;
        term.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        perror("epoll_create1");
        exit(1);
    }
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) != 0) {
        perror("epoll_ctl");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term);
        exit(1);
    }
    ev.data.fd = stdio_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, stdio_fd, &ev) != 0) {
        perror("epoll_ctl");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term);
        exit(1);
    }

    do {
        static struct epoll_event events[10];
        int nevs = epoll_wait(epfd, events, lengthof(events), -1);
        if (nevs < 0) {
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nevs; ++i) {
            static char buf[BUFSIZ];
            ssize_t read_len, written_len;
            if (events[i].data.fd == STDIN_FILENO) {
                read_len = read(STDIN_FILENO, buf, sizeof(buf));
                if (read_len < 0) {
                    perror("read");
                    continue;
                }
                switch (buf[0]) {
                case 3:
                    is_running = 0;
                    printf("^C\r\n");
                    break;
                default:
                    written_len = write(stdio_fd, buf, read_len);
                    if (written_len != read_len) {
                        if (written_len < 0) {
                            perror("write");
                        } else {
                            DEBUG("written shorter %zu < %zu", written_len, read_len);
                        }
                    }
                    break;
                }
            } else if (events[i].data.fd == stdio_fd) {
                read_len = read(stdio_fd, buf, sizeof(buf));
                if (read_len < 0) {
                    perror("read");
                    continue;
                }
                written_len = write(STDOUT_FILENO, buf, read_len);
                if (written_len != read_len) {
                    if (written_len < 0) {
                        perror("write");
                    } else {
                        DEBUG("written shorter %zu < %zu", written_len, read_len);
                    }
                }
            } else {
                DEBUG("unknown fd %d", events[i].data.fd);
            }
        }
    } while (is_running == 1);

    close(epfd);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_term);
    close(stdio_fd);

    return 0;
}
