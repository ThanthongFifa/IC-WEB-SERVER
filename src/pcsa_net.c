// from in class

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "pcsa_net.h"

#define LISTEN_QUEUE 5
int open_listenfd(char *port) {
    struct addrinfo hints;
    struct addrinfo* listp;

    memset(&hints, 0, sizeof(struct addrinfo));
    /* Look to accept connect on any IP addr using this port no */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV; 
    int retCode = getaddrinfo(NULL, port, &hints, &listp);

    if (retCode < 0) {
        fprintf(stderr, "Error: %s\n", gai_strerror(retCode));
        exit(-1);
    }

    int listenFd;
    struct addrinfo *p;
    for (p=listp; p!=NULL; p = p->ai_next) {
        if ((listenFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) 
            continue; /* This option doesn't work; try next */

        int optVal = 1;
        /* Alleviate "Address already in use" by allowing reuse */
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR,
                  (const void *) &optVal, sizeof(int));

        if (bind(listenFd, p->ai_addr, p->ai_addrlen) == 0)
            break; /* Yay, success */

        close(listenFd); /* Bind failed, close this, then next */
    }
    
    freeaddrinfo(listp);

    if (!p) 
        return -1; /* None of them worked. Meh */

    /* Make it ready to accept incoming requests */
    if (listen(listenFd, LISTEN_QUEUE) < 0) {
        close(listenFd);
        return -1;
    }

    /* All good, return the file descriptor */
    return listenFd;
}

int open_clientfd(char *hostname, char *port) {
    struct addrinfo hints;
    struct addrinfo* listp;

    memset(&hints, 0, sizeof(struct addrinfo));
    /* Look to accept connect on any IP addr using this port no */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV; 
    int retCode = getaddrinfo(hostname, port, &hints, &listp);

    if (retCode < 0) {
        fprintf(stderr, "Error: %s\n", gai_strerror(retCode));
        exit(-1);
    }

    int clientFd;
    struct addrinfo *p;
    for (p=listp; p!=NULL; p = p->ai_next) {
        if ((clientFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) 
            continue; /* This option doesn't work; try next */

        /* Try connecting */
        if (connect(clientFd, p->ai_addr, p->ai_addrlen) != -1) 
            break; /* Yay, success */

        close(clientFd); /* Bind failed, close this, then next */
    }
    
    freeaddrinfo(listp);

    if (!p) 
        return -1; /* None of them worked. Meh */

    return clientFd;
}

void write_all(int connFd, char *buf, size_t len) {
    size_t toWrite = len;

    while (toWrite > 0) {
        ssize_t numWritten = write(connFd, buf, toWrite);
        if (numWritten < 0) { fprintf(stderr, "Meh, can't write\n"); return ;}
        toWrite -= numWritten;
        buf += numWritten;
    }
}

// old read_line + parse.c = this read_line
ssize_t read_line(int connFd, char *usrbuf, size_t maxlen) {
    enum {
		STATE_START = 0, STATE_CR, STATE_CRLF//, STATE_CRLFCR, STATE_CRLFCRLF
	};

    int i = 0, state;
    char ch;
    memset(usrbuf, 0, 8192);
    int num;
    int max = maxlen-1;

    state = STATE_START; // start state here
	while (state != STATE_CRLF) {
        char expected = 0; // keep expected next char '\r' OR '\n'

        if (i == max){ // if almost at MAXBUF, break
			break;
        }

        if ( ( num = read(connFd, &ch, 1) ) < 0){break;} // if cant read, break

        usrbuf[i++] = ch; // use for iterate user input (userbuf)

        switch (state) {
		case STATE_START:
		//case STATE_CRLF:
			expected = '\r';
			break;
		case STATE_CR:
		//case STATE_CRLFCR:
			expected = '\n';
			break;
		default:
			state = STATE_START;
			continue;
		}

        // if found \r, expect \n next, then move state else restart at first stage
		if (ch == expected){
			state++; // STATE_START--->STATE_CR--->STATE_CRLF--->finish
        }
		else{
			state = STATE_START;
        }
    
    }
    usrbuf[i] = '\0'; // mark the end of usrbuf
    return i;
    
}
//Old read_line
    // int n;
    // char c, *bufp = usrbuf;
    // for (n = 1; n < maxlen; n++) { 
    //     int numRead;
    //     if ((numRead = read(connFd, &c, 1)) == 1) {
    //         *bufp++ = c;
    //         if (c == '\n') { n++; break; }
    //     } 
    //     else if (numRead == 0) { break; } /* EOF */
    //     else return -1;	  /* Error */
    // }
    // *bufp = '\0';
    // return n-1;
    /*
    //from parse.c

Request * parse(char *buffer, int size, int socketFd) {
    //Differant states in the state machine
    enum {
		STATE_START = 0, STATE_CR, STATE_CRLF, STATE_CRLFCR, STATE_CRLFCRLF
	};

	int i = 0, state;
	size_t offset = 0;
	char ch;
	char buf[8192];
	memset(buf, 0, 8192);

	state = STATE_START;
	while (state != STATE_CRLFCRLF) {
		char expected = 0;

		if (i == size)
			break;

		ch = buffer[i++];
		buf[offset++] = ch;

		switch (state) {
		case STATE_START:
		case STATE_CRLF:
			expected = '\r';
			break;
		case STATE_CR:
		case STATE_CRLFCR:
			expected = '\n';
			break;
		default:
			state = STATE_START;
			continue;
		}

		if (ch == expected)
			state++;
		else
			state = STATE_START;


    */