//
// Created by Zac Jones on 12/13/23.
//

#include <stdio.h>
#include "chatlib.h"
#define _POSIX_C_SOURCE 200112L
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// sets a socket to non-blocking mode
// also enables TCP_NODELAY
int socketSetNonBlockNoDelay(int fd) {
    // flags will be used to store socket flags
    int flags, yes = 1;

    // fcntl is a system call for manipulating file descriptors
    // F_GETFL gets file status flags
    // if fcntl returns -1 that's an error so we return it too
    if ((flags = fcntl(fd, F_GETFL)) == -1) return -1;

    // set the socket to non-blocking mode
    // OR the flags with O_NONBLOCK to add non-blocking attribute
    // if that blows up with a -1, return our own -1
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) return -1;

    // this *disables* an algorithm that is used to accumulate small packets
    // into TCP buffer until the full packet is ready to send
    // meaning we send data as soon as possible, even if the packet is not full
    // it reduces latency at the cost of increased network traffic

    // but note that we don't check for errors, so this is a best effort thing
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return 0;
}