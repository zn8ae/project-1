#include "lisod.h"

#define LISTENQ 1024

// server basic subroutine 
int open_socket(int http_port);
int close_socket(int id, pool *pool);
void init_pool(int listen_fd, pool *pool);
void add_client(int client_socket, pool *pool, struct sockaddr_in *client_addr);
void handle_clients(pool *pool);
void process_request(int i, pool *pool, HTTPContext *context);

// server request handler 
void serve_request_handler(int client_fd, HTTPContext *context);
void serve_get_handler(int client_fd, HTTPContext *context);
void serve_head_handler(int client_fd, HTTPContext *context);
int serve_body_handler(int client_fd, HTTPContext *context);
void serve_post_handler(int client_fd, HTTPContext *context);
void serve_error_handler(int client_fd, HTTPContext *context, char *errnum, char *shortmsg, char *longmsg);

// parser methods 
int parse_uri(pool *pool, char *uri, char* filename);
int parse_header(int socket_fd, Request *request, HTTPContext *context);

// util methods 
void get_time(char *date);
void get_filetype(char *filename, char *filetype);
int is_valid_method(char *method);
char *get_header_value_by_key(char *key, Request *request);

/* file pointer to write log */
FILE *fp;


// server basic subroutine 
int open_socket(int http_port) {
    int sock_fd;
    struct sockaddr_in addr;
    int yes = 1;
    // create a socket 
    // int sockfd = socket(domain, type, protocol)
    // So the correct thing to do is to use AF_INET in your struct sockaddr_in and PF_INET in your call to socket()
    sock_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        Log(fp, "Fail to create new socket\n");
        return EXIT_FAILURE;
    }
    // This helps in manipulating options for the socket referred by the file descriptor sockfd. 
    // This is completely optional, but it helps in reuse of address and port. Prevents error such as: “address already in use”.
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        Log(fp, "Fail to set up socketopt\n");
        return EXIT_FAILURE;
    }
    addr.sin_family = AF_INET;
    addr.sin_port = htons(http_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    // servers bind sockets to ports---notify the OS they accept connections 
    // The bind function assigns a local protocol address to a socket
    if (bind(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(sock_fd);
        Log(fp, "Fail to bind to socket\n");
        return EXIT_FAILURE;
    }
    // The listen function is called only by a TCP server to listen for the client request
    if (listen(sock_fd, LISTENQ) < 0) {
        close(sock_fd);
        Log(fp, "Fail to listen on socket\n");
        return EXIT_FAILURE;
    }
    return sock_fd;
}

int close_socket(int id, pool *pool) {
    FD_CLR(pool->clientfd[id], &pool->read_set);
    if (close(pool->clientfd[id]) < 0) {
        Log(fp, "Failed closing socket.\n");
        return 1;
    }
    Log(fp, "Close connection to client on socket: %d.\n", pool->clientfd[id]);
    pool->clientfd[id] = -1;
    return 0;
}

// setup the initial value for pool attributes                      
void init_pool(int listen_fd, pool *pool) {
    int i = 0;
    pool->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++) pool->clientfd[i] = -1;  
    pool->maxfd = listen_fd;
    // Initializes the file descriptor set fdset to have zero bits for all file descriptors
    FD_ZERO(&pool->read_set);
    // Sets the bit for the file descriptor fd in the file descriptor set fdset
    FD_SET(listen_fd, &pool->read_set);
}

// add a new client to the pool and update pool attributes                                             
void add_client(int client_socket, pool *pool, struct sockaddr_in *client_addr) {
    int i = 0;
    pool->nready--;
    // only accept FD_SETSIZE - 5 clients to keep server from overloading
    for (i = 0; i < (FD_SETSIZE - 5); i++) {
        if (pool->clientfd[i] < 0) {
            // add client descriptor to the pool
            pool->clientfd[i] = client_socket;
	    // add the descriptor to the descriptor set
            FD_SET(client_socket, &pool->read_set);
	    // update max descriptor and pool highwater mark
            if (i > pool->maxi)
                pool->maxi = i;
            if (client_socket > pool->maxfd)
                pool->maxfd = client_socket;
            break;
        }
    }
}
                                                   
// process the ready set from the set of descriptors                                                  
void handle_clients(pool *pool) {
    int i, cur_fd;
    for (i = 0; (pool->nready > 0)&&!(i > pool->maxi); i++) {
        cur_fd = pool->clientfd[i];
        if ((cur_fd > 0) && (FD_ISSET(cur_fd, &pool->ready_set))) {
            pool->nready--;
            HTTPContext *context = (HTTPContext *)calloc(1, sizeof(HTTPContext));
            context->keep_alive = 1;
            context->is_valid = 1;
            process_request(i, pool, context);
            if (!context->keep_alive) {
                close_socket(i, pool);
            }
            free(context);
        }
    }
}

// handle a single request and return responses                    
void process_request(int i, pool *pool, HTTPContext *context) {
    int data;
    char filename[BUFF_SIZE], buf[BUFF_SIZE];
    struct stat sbuf;
    int cur_fd = pool->clientfd[i];
    // The recv function is used to receive data over stream sockets or CONNECTED datagram sockets
    // read data using ith socket and stored information in buffer
    data = recv(cur_fd, buf, BUFF_SIZE, 0);
    if (data == 0) {
        return;
    }
    if (data < 0) {
        context->keep_alive = 0;
        Log(fp, "Error occurred when receiving data from client\n");
        serve_error_handler(cur_fd, context, "500", "Internal Server Error", "The server has encountered an unexpected error.");
        return;
    }
    Log(fp, "Server received %d bytes data on socket %d\n", (int)data, cur_fd);
    // parser handle the grammar check in request data 
    Request *request = parse(buf, data, context);
    if (!context->is_valid) {
        context->keep_alive = 0;
        serve_error_handler(cur_fd, context, "400", "Bad Request", "The request line or header has error");
    }
    strcpy(context->method, request->http_method);
    strcpy(context->version, request->http_version);
    strcpy(context->uri, request->http_uri);
    // check HTTP method (support GET, POST, HEAD now)
    if (!is_valid_method(request->http_method)) {
        context->keep_alive = 0;
        serve_error_handler(cur_fd, context, "501", "Method Unimplemented", "HTTP method is not implemented by the server");
        return;
    }
    // check HTTP version
    if (strcasecmp(context->version, "HTTP/1.1")) {
        context->keep_alive = 0;
        serve_error_handler(cur_fd, context, "505", "HTTP Version not supported", "The current http version HTTP/1.0 is not supported by Liso server");
        return;
    }
    // parse uri (get filename and parameters if any)
    parse_uri(pool, request->http_uri, filename);
    strcpy(context->filename, filename);
    if (stat(context->filename, &sbuf) < 0) {
        context->keep_alive = 0;
        serve_error_handler(cur_fd, context, "404", "Page Not Found", "File is not found on Liso Server");
        return;
    }
    // parse request headers
    if (parse_header(cur_fd, request, context) != 0) return;
    // send response
    serve_request_handler(cur_fd, context);
    return;
}

/* server request handler */
void serve_request_handler(int client_fd, HTTPContext *context) {
    if (!strcasecmp(context->method, "GET")) serve_get_handler(client_fd, context);
    if (!strcasecmp(context->method, "HEAD")) serve_head_handler(client_fd, context);
    if (!strcasecmp(context->method, "POST")) serve_post_handler(client_fd, context);
}

// return response for GET request                                 
void serve_get_handler(int client_fd, HTTPContext *context) {
    serve_head_handler(client_fd, context);
    serve_body_handler(client_fd, context);
}

// return response header to client 
void serve_head_handler(int client_fd, HTTPContext *context) {
    struct tm tm;
    struct stat sbuf;
    char buffer[BUFF_SIZE];
    char filetype[MIN_LINE];
    char tbuf[DATE_SIZE];
    char date[DATE_SIZE];
    get_filetype(context->filename, filetype);
    // get modified time of file
    stat(context->filename, &sbuf);
    tm = *gmtime(&sbuf.st_mtime);
    strftime(tbuf, DATE_SIZE, "%a, %d %b %Y %H:%M:%S GMT", &tm);
    get_time(date);
    // HTTP response header
    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    sprintf(buffer, "%sServer: Liso/1.0\r\n", buffer);
    sprintf(buffer, "%sDate: %s\r\n", buffer, date);
    if (!context->keep_alive) sprintf(buffer, "%sConnection: close\r\n", buffer);
    else sprintf(buffer, "%sConnection: keep-alive\r\n", buffer);
    sprintf(buffer, "%sContent-Length: %lld\r\n", buffer, sbuf.st_size);
    sprintf(buffer, "%sContent-Type: %s\r\n", buffer, filetype);
    sprintf(buffer, "%sCache-Control: no-cache\r\n", buffer);
    sprintf(buffer, "%sLast-Modified: %s\r\n\r\n", buffer, tbuf);
    send(client_fd, buffer, strlen(buffer), 0);
}

// return response body to client
int serve_body_handler(int client_fd, HTTPContext *context) {
    int fd, filesize;
    char *ptr;
    struct stat sbuf;
    if ((fd = open(context->filename, O_RDONLY, 0)) < 0) {
        Log(fp, "Error: Cann't open file \n");
        serve_error_handler(client_fd, context, "500", "Internal Server Error", "The server encountered an unexpected condition");
        return -1;
    }
    stat(context->filename, &sbuf);
    filesize = sbuf.st_size;
    ptr = mmap(0, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    //notebook
    send(client_fd, ptr, filesize, 0);
    munmap(ptr, filesize);
    return 0;
}

// return response for POST request
void serve_post_handler(int client_fd, HTTPContext *context) {
    char buffer[BUFF_SIZE];
    char date[DATE_SIZE];
    get_time(date);
    sprintf(buffer, "HTTP/1.1 200 No Content\r\n");
    sprintf(buffer, "%sServer: Liso/1.0\r\n", buffer);
    sprintf(buffer, "%sDate: %s\r\n", buffer, date);
    if (!context->keep_alive) sprintf(buffer, "%sConnection: close\r\n", buffer);
    else sprintf(buffer, "%sConnection: keep-alive\r\n", buffer);
    sprintf(buffer, "%sContent-Length: 0\r\n", buffer);
    sprintf(buffer, "%sContent-Type: text/html\r\n\r\n", buffer);
    send(client_fd, buffer, strlen(buffer), 0);
}
                                                   
// return error message to client                                     
void serve_error_handler(int client_fd, HTTPContext *context, char *errnum, char *shortmsg, char *longmsg) {
    char buffer[BUFF_SIZE], body[BUFF_SIZE], date[BUFF_SIZE];
    // HTTP error response body
    sprintf(body, "<html><title>Server Error</title>");
    sprintf(body, "%s<body>\r\n", body);
    sprintf(body, "%sError %s -- %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<br><p>%s</p></body></html>\r\n", body, longmsg);
    get_time(date);
    // HTTP error response header
    sprintf(buffer, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    sprintf(buffer, "%sDate: %s\r\n", buffer, date);
    sprintf(buffer, "%sServer: Liso/1.0\r\n", buffer);
    if (!context->keep_alive) sprintf(buffer, "%sConnection: close\r\n", buffer);
    sprintf(buffer, "%sContent-type: text/html\r\n", buffer);
    sprintf(buffer, "%sContent-length: %d\r\n\r\n", buffer, (int) strlen(body));
    send(client_fd, buffer, strlen(buffer), 0);
    send(client_fd, body, strlen(body), 0);
}

/* request parser methods */
int parse_uri(pool *pool, char *uri, char* filename) {
    strcpy(filename, pool->www);
    if (uri[strlen(uri)-1] == '/') {
        strcat(filename, "index.html");
        return 0;
    }
    char *pt = uri;
    while (*pt == '/' || *pt == '.') {pt++;}
    strcat(filename, pt);
    return 0;
}

int parse_header(int socket_fd, Request *request, HTTPContext *context) {
    char *value;
    if ((value = get_header_value_by_key("Connection", request)) != NULL) {
        if (strstr(value, "close")) {
            context->keep_alive = 0;
        }
    }
    return 0;
}

/* util methods */
void get_time(char *date) {
    struct tm tm;
    time_t now;
    now = time(0);
    tm = *gmtime(&now);
    strftime(date, DATE_SIZE, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) strcpy(filetype, "text/html");
    else if (strstr(filename, ".js")) strcpy(filetype, "application/javascript");
    else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png")) strcpy(filetype, "image/png");
    else if (strstr(filename, ".css")) strcpy(filetype, "text/css");
    else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
    else strcpy(filetype, "text/plain");
}

int is_valid_method(char *method) {
    if (!strcasecmp(method, "GET")) return 1;
    else if (!strcasecmp(method, "HEAD")) return 1;
    else if (!strcasecmp(method, "POST")) return 1;
    else return 0;
}

char *get_header_value_by_key(char *key, Request *request) {
    int count = request->header_count;
    int index;
    for (index = 0; index < count; index++) {
        if (strstr(request->headers[index].header_name, key)) {
            return request->headers[index].header_value;
        }
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    // To make a process a TCP server, you need to follow the steps given below −
    // Create a socket with the socket() system call.
    // Bind the socket to an address using the bind() system call. For a server socket on the Internet, an address consists of a port number on the host machine.
    // Listen for connections with the listen() system call.
    // Accept a connection with the accept() system call. This call typically blocks until a client connects with the server.
    // Send and receive data using the read() and write() system calls.
    int listen_sock;
    int new_fd;
    int http_port;
    char* logfile;
    static pool pool;
    struct sockaddr client_addr;
    socklen_t addrlen;
    // extract http port number from the first argument
    http_port = atoi(argv[1]);
    // extract log file from the second argument
    logfile = argv[3];
    // open and create log file descriptor
    fp = fopen(logfile, "w");
    if (fp == NULL) {
        fprintf(stdout, "There is an error happen when trying to open log file.\n");
        exit(EXIT_FAILURE);
    }
    Log(fp, "Start Liso Server\n");
    listen_sock = open_socket(http_port); 
    // setup the initial value for pool attributes                                                                                                              *
    init_pool(listen_sock, &pool);
    pool.www = argv[5];
    // make sure the last char of www is '/'
    if (pool.www[strlen(pool.www)-1] != '/') {
        strcat(pool.www, "/");
    }
    while (1) {
        pool.ready_set = pool.read_set;
        // The select function indicates which of the specified file descriptors is ready for reading, ready for writing, or has an error condition pending.
        pool.nready = select(pool.maxfd+1, &pool.ready_set, NULL, NULL, NULL);
        if (pool.nready == -1) {
            fprintf(stderr, "select error\n");
            Log(fp, "Error happened when performing select()\n");
            return EXIT_FAILURE;
        }
        // listen discriptor ready 
        // Returns a non-zero value if the bit for the file descriptor fd is set in the file descriptor set pointed to by fdset, and 0 otherwise.
        if (FD_ISSET(listen_sock, &pool.ready_set) != 0) {
            addrlen = sizeof client_addr;
	    // The accept function is called by a TCP server to return the next completed connection from the front of the completed connection queue.
            new_fd = accept(listen_sock, (struct sockaddr *)&client_addr, &addrlen);
            if (new_fd == -1) {
                fprintf(stderr, "establishing new connection error\n");
                Log(fp, "Error happened when trying to establish a new client connection.\n");
                break;
            }
            else {
                // add new client to pool 
		// add a new client to the pool and update pool attributes 
                add_client(new_fd, &pool, (struct sockaddr_in *) &client_addr);
            }
        }
        handle_clients(&pool);
    }
    close(listen_sock);
    return EXIT_SUCCESS;
}
