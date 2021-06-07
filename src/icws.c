//base on inclass micro_cc.c
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include "parse.h"
#include "pcsa_net.h"

/* Rather arbitrary. In real life, be careful with buffer overflow */
#define MAXBUF 8192

typedef struct sockaddr SA;

void get_file_local(char* loc, char* rootFol, char* req_obj){ //get file location
    strcpy(loc, rootFol);               // loc = rootFol
    if (strcmp(req_obj, "/") == 0){     // if input == / then req_obj = /index
        req_obj = "/index.html";
    } 
    else if (req_obj[0] != '/'){      // add '/' if first char is not '/'
        strcat(loc, "/");   
    }
    strcat(loc, req_obj);
    printf(" File location is: %s \n", loc);
}

char* get_filename_ext(char *filename){ // return filename ext
    char* name = filename;
    while (strrchr(name, '.') != NULL){
        name = strrchr(name, '.');
        name = name + 1; 
    }

    return name;
}

int write_header(char* headr, int fd, char* loc){ // return -1 if error
    // check if file exist
    if (fd < 0){
        sprintf(headr, 
                "HTTP/1.1 404 not found\r\n"
                "Server: icws\r\n"
                "Connection: close\r\n");
        return -1;
    }

    //check file size
    struct stat st;
    fstat(fd, &st);
    size_t filesize = st.st_size;
    if (filesize < 0){
        sprintf(headr, 
            "HTTP/1.1 999 file size error\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n");
        return -1;
    }

    char * ext = get_filename_ext(loc);
    char * mime;

     // check extension
    if ( strcmp(ext, "html") == 0 )
        mime = "text/html";
    else if ( strcmp(ext, "jpg") == 0 )
        mime = "image/jpeg";
    else if ( strcmp(ext, "jpeg") == 0 )
        mime = "image/jpeg";
    else{
        mime = "null";
    }

     if ( strcmp(mime, "null")==0){
        sprintf(headr, 
            "HTTP/1.1 999 file type not support\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n");
        return -1;
    }

    sprintf(headr, 
            "HTTP/1.1 200 OK\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n\r\n", filesize, mime);
    
    return 0;
}

void send_header(int connFd, char* rootFol, char* req_obj){ // respind with ONLY header
    char local[MAXBUF];

    get_file_local(local, rootFol, req_obj); //keep file location in local

    int fd = open(local , O_RDONLY); // open req_obg in rootFol

    char headr[MAXBUF]; // this is the header
    write_header( headr, fd, local ); // this write header to headr
    write_all(connFd, headr, strlen(headr)); // send headr

    if ( (close(fd)) < 0 ){ // closing
        printf("Failed to close\n");
    }
}

void send_get(int connFd, char* rootFol, char* req_obj) {
    char local[MAXBUF];

    get_file_local(local, rootFol, req_obj);

    int fd = open( local , O_RDONLY);

    char headr[MAXBUF];
    int result = write_header( headr, fd, local );
    write_all(connFd, headr, strlen(headr));

    if (result < 0){
        if ( (close(fd)) < 0 ){
            printf("Failed to close input\n");
        }
        return;
    }

    char buf[MAXBUF];
    ssize_t numRead;
    while ((numRead = read(fd, buf, MAXBUF)) > 0) {
        write_all(connFd, buf, numRead);
    }
    if ( (close(fd)) < 0 ){
        printf("Failed to close input\n");
    }
}
    

    

void serve_http(int connFd, char* rootFol){
    char buf[MAXBUF];
    int readRet = read(connFd,buf,8192);

    Request *request = parse(buf,readRet,connFd);

    if (request == NULL){
        printf("LOG: Failed to parse request\n");
        return;
    }
    if (strcasecmp( request->http_method , "GET") == 0 && strcasecmp( request->http_version , "HTTP/1.1") == 0) {

        }
    else if (strcasecmp( request->http_version , "HTTP/1.1") != 0){
        printf("LOG: Incompatible HTTP version\n");
        return;
    }

    if (strcasecmp( request->http_method , "GET") == 0 ) {
        printf("LOG: GET method requested\n");
        send_get(connFd, rootFol, request->http_uri );
    }
    else if (strcasecmp( request->http_method , "HEAD") == 0 ) {
        printf("LOG: HEAD method requested\n");
        send_header(connFd, rootFol, request->http_uri );
    }
    else {
        printf("LOG: Unknown request\n\n");
        sprintf(headr, 
            "HTTP/1.1 501 Method not implemented\r\n"
            "Server: Micro\r\n"
            "Connection: close\r\n");
        write_all(connFd, headr, strlen(headr));
    }

    free(request->headers);
    free(request);
}


struct survival_bag {
        struct sockaddr_storage clientAddr;
        int connFd;
};

char* dirName;

void* conn_handler(void *args) {
    struct survival_bag *context = (struct survival_bag *) args;
    
    pthread_detach(pthread_self());
    serve_http(context->connFd, dirName);

    close(context->connFd);
    
    free(context); /* Done, get rid of our survival bag */

    return NULL; /* Nothing meaningful to return */
}

// as server: ./icws 22701 ./sample-www/
// as client: telnet localhost 22701

int main(int argc, char* argv[]) {
    int listenFd = open_listenfd(argv[1]);

    if (argc >= 3){
        dirName = argv[2];
    }
    else{
        dirName = "./";
    }

    for (;;) {

        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);
        pthread_t threadInfo;

        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);

        if (connFd < 0) { fprintf(stderr, "Failed to accept\n"); continue; }
        struct survival_bag *context = 
                    (struct survival_bag *) malloc(sizeof(struct survival_bag));
        context->connFd = connFd;
        
        memcpy(&context->clientAddr, &clientAddr, sizeof(struct sockaddr_storage));
        
        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *) &clientAddr, clientLen, 
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0)==0) 
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
                
        pthread_create(&threadInfo, NULL, conn_handler, (void *) context);
    }

    return 0;
}