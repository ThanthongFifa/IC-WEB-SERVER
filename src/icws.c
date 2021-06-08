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
#include <time.h>
#include "parse.h"
#include "pcsa_net.h"

//---------- DEBUG TOOLS----------
//#define YACCDEBUG
#define YYERROR_VERBOSE
#ifdef YACCDEBUG
#define YPRINTF(...) printf(__VA_ARGS__)
#else
#define YPRINTF(...)
#endif
//--------------------------------

/* Rather arbitrary. In real life, be careful with buffer overflow */
#define MAXBUF 8192

typedef struct sockaddr SA;

char* get_mime(char* ext){ // return mime
    if ( strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0 ){
        return "text/html";
    }
    else if ( strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0){
        return "image/jpeg";
    }
    else if ( strcmp(ext, "css") == 0 ){
        return "text/css";
    }
    else if ( strcmp(ext, "csv") == 0 ){
        return "text/csv";
    }
    else if ( strcmp(ext, "txt") == 0 ){
        return "text/plain";
    }
    else if ( strcmp(ext, "png") == 0 ){
        return "image/png";
    }
    else if ( strcmp(ext, "gif") == 0 ){
        return "image/gif";
    }
    else{
        return "null";
    }
}


char* today(){
    char ans[100];
    time_t t;

    time(&t);

    struct tm *local = localtime(&t);
    sprintf(ans, "%d/%d/%d", local->tm_mday, local->tm_mon + 1, local->tm_year + 1900);
    
    return strdup(ans);
}

void get_file_local(char* loc, char* rootFol, char* req_obj){ //get file location
    strcpy(loc, rootFol);               // loc = rootFol
    if (strcmp(req_obj, "/") == 0){     // if input == / then req_obj = /index
        req_obj = "/index.html";
    } 
    else if (req_obj[0] != '/'){      // add '/' if first char is not '/'
        strcat(loc, "/");   
    }
    strcat(loc, req_obj);
    //printf("File location is: %s \n", loc);
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
    // if can open
    if (fd < 0){
        sprintf(headr, 
                "HTTP/1.1 404 not found\r\n"
                "Date: %s\r\n"
                "Server: icws\r\n"
                "Connection: close\r\n", today());
        return -1;
    }

    //check file size
    struct stat st;
    fstat(fd, &st);
    size_t filesize = st.st_size;
    if (filesize < 0){
        sprintf(headr, 
            "HTTP/1.1 400 file size error\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n", today());
        return -1;
    }

    // get mime
    char* ext = get_filename_ext(loc);
    char* mime;
    mime = get_mime(ext);

    // if supported mime
    if ( strcmp(mime, "null") == 0){
        sprintf(headr, 
            "HTTP/1.1 400 file type not support\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n", today());
        return -1;
    }

    // if nothing wrong
    char* last_mod = ctime(&st.st_mtime); // get last modified

    sprintf(headr, 
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n\r\n", today(), filesize, mime, last_mod);
    
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

    // send body
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
    char line[MAXBUF];

    while ( read_line(connFd, line, MAXBUF) > 0 ){
        strcat(buf, line);
        if (strcmp(line, "\r\n") == 0){ 
            break; 
        }
    }

    Request *request = parse(buf,MAXBUF,connFd);
    char headr[MAXBUF];

    if (request == NULL){ // check parsing fail
        printf("LOG: Failed to parse request\n");
        sprintf(headr, 
            "HTTP/1.1 400 Parsing Failed\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n", today());
        write_all(connFd, headr, strlen(headr));
        return;
    }
    else if (strcasecmp( request->http_version , "HTTP/1.1") != 0){ // check HTTP version
        printf("LOG: Incompatible HTTP version\n");
        sprintf(headr, 
            "HTTP/1.1 505 incompatable version\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n", today());
        write_all(connFd, headr, strlen(headr));
        return;
    }

    if (strcasecmp( request->http_method , "GET") == 0 ) { // handle GET request
        printf("LOG: GET method requested\n");
        send_get(connFd, rootFol, request->http_uri );
    }
    else if (strcasecmp( request->http_method , "HEAD") == 0 ) { // handle HEAD request
        printf("LOG: HEAD method requested\n");
        send_header(connFd, rootFol, request->http_uri );
    }
    else {
        printf("LOG: Unknown request\n\n");
        sprintf(headr, 
            "HTTP/1.1 501 Method not implemented\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: close\r\n", today());
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

/* as server: ./icws localhost 22701 ./sample-www
   as client: telnet localhost 22701
              netcat localhost [portnum] < [filename]
              GET /<filename> HTTP/1.1
              HEAD /<filename> HTTP/1.1
*/
int main(int argc, char* argv[]) {
    int listenFd = open_listenfd(argv[2]);

    if (argc >= 3){
        dirName = argv[3];
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


/*
---------------- Disclamer ----------------

this code is base on inclass micro_cc.c

------------ People that help me ------------

- Thanawin Boonpojanasoontorn  (6280163)
- Vanessa Rujipatanakul (6280204) ------ she help me alot

------------ References ------------

https://github.com/Yan-J/Networks-HTTP-Server
https://datatracker.ietf.org/doc/html/rfc2046#section-5.1.1
https://man7.org/linux/man-pages/man3/getaddrinfo.3.html
https://www.w3schools.com/tags/ref_httpmethods.asp
http://beej.us/guide/bgnet/html/
https://www.google.com/search?q=impliment+web+server+support+GET+and+HEAD+github&oq=impliment+web+server&aqs=chrome.0.69i59j0i13j69i57j0i13j69i59j0i22i30l5.27028j1j7&sourceid=chrome&ie=UTF-8
https://stackoverflow.com/questions/423626/get-mime-type-from-filename-in-c
https://stackoverflow.com/questions/1442116/how-to-get-the-date-and-time-values-in-a-c-program
https://pubs.opengroup.org/onlinepubs/007908799/xsh/getdate.html

*/
