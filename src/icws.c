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
#include <poll.h>
#include <getopt.h>

#include <sys/wait.h>

int poison;

char *port;
char *numThread;
char *timeOut;
char* dirName;
char* cgi_dirName;

int timeout;

/* Rather arbitrary. In real life, be careful with buffer overflow */
#define MAXBUF 8192
#define PERSISTENT 1
#define CLOSE 0

//---------- thread pool ----------
#define MAXTHREAD 256
int num_thread;

pthread_t thread_pool[MAXTHREAD];
pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_parse = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

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
}

char* get_filename_ext(char *filename){ // return filename ext
    char* name = filename;
    while (strrchr(name, '.') != NULL){
        name = strrchr(name, '.');
        name = name + 1; 
    }

    return name;
}

int write_header(char* headr, int fd, char* loc, char* connection_str){ // return -1 if error
    // if can open
    if (fd < 0){
        sprintf(headr, 
                "HTTP/1.1 404 not found\r\n"
                "Date: %s\r\n"
                "Server: icws\r\n"
                "Connection: %s\r\n", today(), connection_str);
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
            "Connection: %s\r\n", today(), connection_str);
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
            "Connection: %s\r\n", today(), connection_str);
        return -1;
    }

    // if nothing wrong
    char* last_mod = ctime(&st.st_mtime); // get last modified

    sprintf(headr, 
            "HTTP/1.1 200 OK\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: %s\r\n"
            "Content-length: %lu\r\n"
            "Content-type: %s\r\n"
            "Last-Modified: %s\r\n\r\n", today(), connection_str, filesize, mime, last_mod);
    
    return 0;
}

void send_header(int connFd, char* rootFol, char* req_obj, char* connection_str){ // respind with ONLY header
    char local[MAXBUF];
   
    get_file_local(local, rootFol, req_obj); //keep file location in local

    int fd = open(local , O_RDONLY); // open req_obg in rootFol

    char headr[MAXBUF]; // this is the header

    write_header( headr, fd, local, connection_str); // this write header to headr
    write_all(connFd, headr, strlen(headr)); // send headr

    if ( (close(fd)) < 0 ){ // closing
        printf("Failed to close\n");
    }
}

void send_get(int connFd, char* rootFol, char* req_obj, char* connection_str) {
    char local[MAXBUF];

    get_file_local(local, rootFol, req_obj);

    int fd = open( local , O_RDONLY);

    char headr[MAXBUF];
    int result = write_header( headr, fd, local, connection_str);
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

void send_post(int connFd, char* rootFol, char* req_obj, char* connection_str){
    //printf("POST\n");
}
    
int serve_http(int connFd, char* rootFol){
    //printf("---------- calling serve_http ----------\n");
    char buf[MAXBUF];
    char line[MAXBUF];
    struct pollfd fds[1];
    int readline;
    //char lastline_end[2];
    
    //check timeout
    for(;;){
        //printf("calling poll()\n");
        fds[0].fd = connFd;
        fds[0].events = POLLIN;

        int t = timeout * 1000;

        int pollret = poll(fds, 1, t);

        if(pollret < 0){
            perror("poll() fail\n");
            return CLOSE;
        } else if(pollret == 0){
            printf("timeout\n");
            return CLOSE;
        } else{
            while ( (readline = read(connFd, line, MAXBUF)) > 0 ){ // fix this part
                strcat(buf, line);

                if (strstr(buf, "\r\n\r\n") != NULL){
                    memset(line, '\0', MAXBUF);
                    break;
                }
                memset(line, '\0', MAXBUF);
            }
            //printf("done reading\n");
            break;
        }
    }


    pthread_mutex_lock(&mutex_parse);
    Request *request = parse(buf,MAXBUF,connFd);
    pthread_mutex_unlock(&mutex_parse);

    //connection type
    int connection;
    char* connection_str;
    connection = PERSISTENT;
    connection_str = "keep-alive";
    // connection = CLOSE;
    // connection_str = "close";

    char* head_name;
    char* head_val;


    char headr[MAXBUF];

    if (request == NULL){ // check parsing fail
        printf("LOG: Failed to parse request\n");
        sprintf(headr, 
            "HTTP/1.1 400 Parsing Failed\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: %s\r\n", today(), connection_str);
        //printf("after sprintf\n");
        write_all(connFd, headr, strlen(headr));
        //printf("after write_all\n");
        memset(buf, 0, MAXBUF);
        memset(headr, 0, MAXBUF);
        // printf("%s\n", connection_str);
        return connection;
    }
    
    // check if close or keep-alive
    for(int i = 0; i < request->header_count;i++){
        head_name = request->headers[i].header_name;
        head_val = request->headers[i].header_value;
        if(strcasecmp(head_name, "CONNECTION") == 0){
            if(strcasecmp(head_val, "CLOSE") == 0){
                connection = CLOSE;
                connection_str = "close";
            }
            break;
        }
    }

    if (strcasecmp( request->http_version , "HTTP/1.1") != 0 && strcasecmp( request->http_version , "HTTP/1.0") != 0){ // check HTTP version
        printf("LOG: Incompatible HTTP version\n");
        printf("request->http_version: %s\n",request->http_version);
        sprintf(headr, 
            "HTTP/1.1 505 incompatable version\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: %s\r\n", today(), connection_str);
        write_all(connFd, headr, strlen(headr));
        free(request->headers);
        free(request);
        memset(buf, 0, MAXBUF);
        memset(headr, 0, MAXBUF);
        return connection;
    }

    if (strcasecmp( request->http_method , "GET") == 0 ) { // handle GET request
        printf("LOG: GET method requested\n");
        send_get(connFd, rootFol, request->http_uri, connection_str);
        printf("done GET\n");
    }
    else if (strcasecmp( request->http_method , "HEAD") == 0 ) { // handle HEAD request
        printf("LOG: HEAD method requested\n");
        send_header(connFd, rootFol, request->http_uri, connection_str);
    }
    else if (strcasecmp( request->http_method , "POST") == 0 ) { // handle POST request
        printf("LOG: POST method requested\n");
        send_post(connFd, rootFol, request->http_uri, connection_str);
    }
    else {
        printf("LOG: Unknown request\n\n");
        sprintf(headr, 
            "HTTP/1.1 501 Method not implemented\r\n"
            "Date: %s\r\n"
            "Server: icws\r\n"
            "Connection: %s\r\n", today(), connection_str);
        write_all(connFd, headr, strlen(headr));
    }

    free(request->headers);
    free(request);
    memset(buf, 0, MAXBUF);
    memset(headr, 0, MAXBUF);
    printf("done\n");
    return connection;
}

struct survival_bag {
        struct sockaddr_storage clientAddr;
        int connFd;
};

struct survival_bag taskQueue[256];
int taskCount = 0;

void* doTask(struct survival_bag* task) {
    //struct survival_bag *context = (struct survival_bag *) args;
    int connection = PERSISTENT;
    
    //pthread_detach(pthread_self());
    while(connection == PERSISTENT){
        //if(task->connFd < 0){break;}
        connection = serve_http(task->connFd, dirName);
    }
    printf("done dotask\n");

    close(task->connFd); // close connection
    
    //free(task); /* Done, get rid of our survival bag */

    return NULL; /* Nothing meaningful to return */
}

void submitTask(struct survival_bag task) {
    pthread_mutex_lock(&mutex_queue);
    taskQueue[taskCount] = task;
    taskCount++;
    pthread_mutex_unlock(&mutex_queue);
    pthread_cond_signal(&condition_var);
}

void* thread_function(void* args) {
    while (1) {
        struct survival_bag task;
        //pthread_detatch(pthread_self);

        pthread_mutex_lock(&mutex_queue);
        while (taskCount == 0) {
            //printf("stuck here\n");
            pthread_cond_wait(&condition_var, &mutex_queue);
        }
        //printf("out here\n");

        task = taskQueue[0];
        int i;
        for (i = 0; i < taskCount - 1; i++) {
            taskQueue[i] = taskQueue[i + 1];
        }
        taskCount--;
        pthread_mutex_unlock(&mutex_queue);

        doTask(&task);

        if(task.connFd < 0){break;}
    }
    // printf("stop work\n");
    // pthread_exit(pthread_self);
}

void sigingHandeler(int sig) {
	printf("\nLOG: Shutting down\n");

	poison = -1;

    struct survival_bag *poisonPill = (struct survival_bag *) malloc(sizeof(struct survival_bag));
    poisonPill->connFd = -1;

    for (int i=0; i < num_thread; i++){
        printf("feed poison\n");
        submitTask(*poisonPill);
    }
}

/* as server:   ./icws --port 22702 --root ./sample-www --numThreads 1 --timeout 10 --cgiHandler ./cgi-demo
                ./icws --port <listenPort> --root <wwwRoot> --numThreads <numThreads> --timeout <timeout> --cgiHandler <cgiProgram>
   as client:   telnet localhost 22702
                curl http://localhost:22702/index.html
                siege -c 10 -r 50 http://localhost:22702/

                netcat localhost [portnum] < [filename]
                GET /<filename> HTTP/1.1
                HEAD /<filename> HTTP/1.1
*/

    

int main(int argc, char* argv[]) {
    //int listenFd = open_listenfd(argv[2]);

    //signal(SIGINT, sigingHandeler);

    // if (argc > 7){
    //     dirName = argv[4];
    //     num_thread = atoi(argv[6]); //make 5 threads
    //     timeout = atoi(argv[8]);
    // }
    // else{
    //     dirName = "./";
    //     num_thread = 5;
    //     timeout = 5;
    // }
    static struct option long_ops[] =
    {
        {"port", required_argument, NULL, 'p'},
        {"root", required_argument, NULL, 'r'}, 
        {"numThreads", required_argument, NULL, 'n'}, 
        {"timeout", required_argument, NULL, 't'}, 
        {"cgiHandler", required_argument, NULL, 'c'},
        {NULL, 0, NULL, 0}
    };
    int ch;
    while ((ch = getopt_long(argc, argv, "p:r:n:t:c:", long_ops, NULL)) != -1){
        switch (ch)
        {
            case 'p':
                printf("port: %s\n", optarg);
                port = optarg;
                break;
            case 'r':
                printf("root: %s\n", optarg);
                dirName = optarg;
                break;
            case 'n':
                printf("numThreads: %s\n", optarg);
                numThread = optarg;
                break;
            case 't':
                printf("timeout: %s\n", optarg);
                timeOut = optarg;
                break;
            case 'c':
                printf("cgi program: %s\n", optarg);
                cgi_dirName = optarg;
                break;
        }
  
    }

    int listenFd = open_listenfd(port);
    int num_thread = atoi(numThread);
    timeout = atoi(timeOut);


    //create thread
    for (int i=0; i < num_thread; i++){
        if(pthread_create(&thread_pool[i], NULL, thread_function, NULL) != 0){
            printf("fail to create thread\n");
        }
    }

    for (;;) {
        // printf("\npoison: %d",poison);
        // if(poison < 0){
        //     printf("found poison\n");
        //     break;
        //     }

        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);
        //pthread_t threadInfo;

        int connFd = accept(listenFd, (SA *) &clientAddr, &clientLen);

        if (connFd < 0) { fprintf(stderr, "Failed to accept\n"); continue; }

        struct survival_bag *context = (struct survival_bag *) malloc(sizeof(struct survival_bag));
        context->connFd = connFd;
        
        memcpy(&context->clientAddr, &clientAddr, sizeof(struct sockaddr_storage));
        
        char hostBuf[MAXBUF], svcBuf[MAXBUF];
        if (getnameinfo((SA *) &clientAddr, clientLen, 
                        hostBuf, MAXBUF, svcBuf, MAXBUF, 0)==0) 
            printf("Connection from %s:%s\n", hostBuf, svcBuf);
        else
            printf("Connection from ?UNKNOWN?\n");
                
        //pthread_create(&threadInfo, NULL, doTask, (void *) context);

        // int * pclient = malloc(sizeof(int));
        // *pclient = connFd;

        //pthread_mutex_lock(&mutex_queue);

        submitTask(*context);
        //pthread_cond_signal(&condition_var);

        //pthread_mutex_unlock(&mutex_queue);


    }
    for (int i=0; i < num_thread; i++){
        if(pthread_join(thread_pool[i], NULL) != 0){
            printf("fail to join thread\n");
        }
    }
    pthread_mutex_destroy(&mutex_queue);
    pthread_mutex_destroy(&mutex_parse);
    pthread_cond_destroy(&condition_var);

    return 0;

    return 0;
}


/*
---------------- BUG ----------------
-get warning when curl http://localhost:1234/cat.jpg

---------------- Disclamer ----------------

this code is base on inclass micro_cc.c

------------ Collaborator List ------------

- Thanawin Boonpojanasoontorn  (6280163)
- Vanessa Rujipatanakul (6280204)
- Krittin Nisunarat (6280782)
- Khwanchanok Chaichanayothinwatchara (6280164)

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
https://www.codeproject.com/Articles/1275479/State-Machine-Design-in-C
https://github.com/Pithikos/C-Thread-Pool
https://github.com/antimattercorrade/concurrent_web_servers

//Thread pool
https://youtu.be/_n2hE2gyPxU
https://youtube.com/playlist?list=PL9IEJIKnBJjH_zM5LnovnoaKlXML5qh17
https://code-vault.net/lesson/j62v2novkv:1609958966824

//Timeout poll()
https://www.youtube.com/watch?v=UP6B324Qh5k 

//Persistant server
https://github.com/nathan78906/HTTPServer/blob/master/PersistentServer.c 

//CGI
https://github.com/klange/cgiserver/blob/master/cgiserver.c

*/
