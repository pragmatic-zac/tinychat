#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/select.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>

#include "chatlib.h"

/* ============================================================================
 * Low level terminal handling.
 *
 * There's a lot of magic here but the general idea is that we want to control
 * the terminal completely ourselves - we don't want the OS doing any of it.
 *
 * We also properly handle a message received (and printed) while the user is
 * in the midst of typing a message...without doing this, the rec'd message would
 * be appended at the end of the in-progress message. This would be bad.
 * ========================================================================== */

void disableRawModeAtExit(void);

/* Raw mode: 1960 magic shit. */
int setRawMode(int fd, int enable) {
    /* We have a bit of global state (but local in scope) here.
     * This is needed to correctly set/undo raw mode. */
    static struct termios orig_termios; // Save original terminal status here.
    static int atexit_registered = 0;   // Avoid registering atexit() many times.
    static int rawmode_is_set = 0;      // True if raw mode was enabled.

    struct termios raw;

    /* If enable is zero, we just have to disable raw mode if it is
     * currently set. */
    if (enable == 0) {
        /* Don't even check the return value as it's too late. */
        if (rawmode_is_set && tcsetattr(fd,TCSAFLUSH,&orig_termios) != -1)
            rawmode_is_set = 0;
        return 0;
    }

    /* Enable raw mode. */
    if (!isatty(fd)) goto fatal;
    if (!atexit_registered) {
        atexit(disableRawModeAtExit);
        atexit_registered = 1;
    }
    if (tcgetattr(fd,&orig_termios) == -1) goto fatal;

    raw = orig_termios;  /* modify the original mode */
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - do nothing. We want post processing enabled so that
     * \n will be automatically translated to \r\n. */
    // raw.c_oflag &= ...
    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * but take signal chars (^Z,^C) enabled. */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

    /* put terminal in raw mode after flushing */
    if (tcsetattr(fd,TCSAFLUSH,&raw) < 0) goto fatal;
    rawmode_is_set = 1;
    return 0;

    fatal:
    errno = ENOTTY;
    return -1;
}

/* At exit we'll try to fix the terminal to the initial conditions. */
void disableRawModeAtExit(void) {
    setRawMode(STDIN_FILENO,0);
}

/* ============================================================================
 * Mininal line editing.
 * ========================================================================== */

void terminalCleanCurrentLine(void) {
    write(fileno(stdout),"\e[2K",4);
}

void terminalCursorAtLineStart(void) {
    write(fileno(stdout),"\r",1);
}

#define IB_MAX 128
struct InputBuffer {
    char buf[IB_MAX];       // Buffer holding the data.
    int len;                // Current length.
};

/* inputBuffer*() return values: */
#define IB_ERR 0        // Sorry, unable to comply.
#define IB_OK 1         // Ok, got the new char, did the operation, ...
#define IB_GOTLINE 2    // Hey, now there is a well formed line to read.

/* Append the specified character to the buffer. */
int inputBufferAppend(struct InputBuffer *ib, int c) {
    if (ib->len >= IB_MAX) return IB_ERR; // No room.

    ib->buf[ib->len] = c;
    ib->len++;
    return IB_OK;
}

void inputBufferHide(struct InputBuffer *ib);
void inputBufferShow(struct InputBuffer *ib);

/* Process every new keystroke arriving from the keyboard. As a side effect
 * the input buffer state is modified in order to reflect the current line
 * the user is typing, so that reading the input buffer 'buf' for 'len'
 * bytes will contain it. */
int inputBufferFeedChar(struct InputBuffer *ib, int c) {
    switch(c) {
        case '\n':
            break;          // Ignored. We handle \r instead.
        case '\r':
            return IB_GOTLINE;
        case 127:           // Backspace.
            if (ib->len > 0) {
                ib->len--;
                inputBufferHide(ib);
                inputBufferShow(ib);
            }
            break;
        default:
            if (inputBufferAppend(ib,c) == IB_OK)
                write(fileno(stdout),ib->buf+ib->len-1,1);
            break;
    }
    return IB_OK;
}

/* Hide the line the user is typing. */
void inputBufferHide(struct InputBuffer *ib) {
    (void)ib; // Not used var, but is conceptually part of the API.
    terminalCleanCurrentLine();
    terminalCursorAtLineStart();
}

/* Show again the current line. Usually called after InputBufferHide(). */
void inputBufferShow(struct InputBuffer *ib) {
    write(fileno(stdout),ib->buf,ib->len);
}

/* Reset the buffer to be empty. */
void inputBufferClear(struct InputBuffer *ib) {
    ib->len = 0;
    inputBufferHide(ib);
}

/* =============================================================================
 * Main program logic, finally :)
 * ========================================================================== */

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        exit(1);
    }

    /* Create a TCP connection with the server. */
    int s = TCPConnect(argv[1],atoi(argv[2]),0);
    if (s == -1) {
        perror("Connecting to server");
        exit(1);
    }

    /* Put the terminal in raw mode: this way we will receive every
     * single key stroke as soon as the user types it. No buffering
     * nor translation of escape sequences of any kind. */
    setRawMode(fileno(stdin),1);

    /* Wait for the standard input or the server socket to
     * have some data. Remember that both stdin and the socket are just
     * file descriptors so select() can wait for both. */
    fd_set readfds;
    int stdin_fd = fileno(stdin);

    struct InputBuffer ib;
    inputBufferClear(&ib);

    while(1) {
        FD_ZERO(&readfds);
        FD_SET(s, &readfds);
        FD_SET(stdin_fd, &readfds);
        int maxfd = s > stdin_fd ? s : stdin_fd;

        // notice that in this select, we don't pass a timeout
        // so this will block until either an exception occurs
        // OR we have a file descriptor ready to be addressed
        int num_events = select(maxfd+1, &readfds, NULL, NULL, NULL);
        if (num_events == -1) {
            perror("select() error");
            exit(1);
        } else if (num_events) {
            /* Generic buffer for both code paths.
             * Either we got a message from the server or a
             * keystroke from the user. */
            char buf[128];

            if (FD_ISSET(s, &readfds)) {
                /* Data from the server? */
                ssize_t count = read(s,buf,sizeof(buf));
                if (count <= 0) {
                    printf("Connection lost\n");
                    exit(1);
                }
                inputBufferHide(&ib);
                write(fileno(stdout),buf,count);
                inputBufferShow(&ib);
            } else if (FD_ISSET(stdin_fd, &readfds)) {
                /* Data from the user typing on the terminal? */
                ssize_t count = read(stdin_fd,buf,sizeof(buf));
                for (int j = 0; j < count; j++) {
                    int res = inputBufferFeedChar(&ib,buf[j]);
                    switch(res) {
                        case IB_GOTLINE:
                            inputBufferAppend(&ib,'\n');
                            inputBufferHide(&ib);
                            write(fileno(stdout),"you> ", 5);
                            write(fileno(stdout),ib.buf,ib.len);
                            write(s,ib.buf,ib.len);
                            inputBufferClear(&ib);
                            break;
                        case IB_OK:
                            break;
                    }
                }
            }
        }
    }

    close(s);
    return 0;
}