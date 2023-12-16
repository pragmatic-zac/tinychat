//
// Created by Zac Jones on 12/13/23.
//

#ifndef TINYCHAT_CHATLIB_H
#define TINYCHAT_CHATLIB_H

/* Networking. */
int createTCPServer(int port);
int socketSetNonBlockNoDelay(int fd);
int acceptClient(int server_socket);
int TCPConnect(char *addr, int port, int nonblock);

/* Allocation. */
void *chatMalloc(size_t size);
void *chatRealloc(void *ptr, size_t size);

#endif //TINYCHAT_CHATLIB_H