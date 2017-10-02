#include "lisod.h"

#define ARG_NUMBER 3
#define LISTENQ 1024

/* server basic subroutine */
int  open_listen_socket(int port);
int  close_client_socket(int id, pool *p);
void init_pool(int listenfd, pool *p);
void add_client(int newfd, pool *p, struct sockaddr_in *cli_addr);
void handle_clients(pool *p);
void process_request(int i, pool *p, HTTPContext *context);

/* server request handler */
void serve_request_handler(int client_fd, HTTPContext *context);
void serve_error_handler(int client_fd, HTTPContext *context, char *errnum, char *shortmsg, char *longmsg);
void serve_get_handler(int client_fd, HTTPContext *context);
void serve_head_handler(int client_fd, HTTPContext *context);
int  serve_body_handler(int client_fd, HTTPContext *context);
void serve_post_handler(int client_fd, HTTPContext *context);

/* request parser methods */
int  parse_uri(pool *p, char *uri, char* filename);
int  parse_header(int socketfd, Request *request, HTTPContext *context);

/* util methods */
void get_time(char *date);
void get_filetype(char *filename, char *filetype);
int  is_valid_method(char *method);
char *get_header_value_by_key(char *key, Request *request);

/* file pointer to write log */
FILE *fp;

int main(int argc, char* argv[]) 
{
    // To make a process a TCP server, you need to follow the steps given below −
    // Create a socket with the socket() system call.
    // Bind the socket to an address using the bind() system call. For a server socket on the Internet, an address consists of a port number on the host machine.
    // Listen for connections with the listen() system call.
    // Accept a connection with the accept() system call. This call typically blocks until a client connects with the server.
    // Send and receive data using the read() and write() system calls.
    int listen_sock;
    int newfd;
    struct sockaddr cli_addr;
    socklen_t addrlen;
    char* logfile;

    int http_port;

    // input argument must be 4
    // if (argc != ARG_NUMBER + 1)
    // {
    //     fprintf(stderr, "usage: ./lisod <HTTP port> <log file> <www folder>\n");
    // 	exit(EXIT_FAILURE);
    // }

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

    static pool pool;
    Log(fp, "Start Liso Server\n");

    listen_sock = open_listen_socket(http_port); 
    // purpose:    setup the initial value for pool attributes                     
    // parameters: sock - the descriptor server using to listen client connections 
    //             p    - pointer to pool instance                                                                                            *
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
        /* listen discriptor ready */
        // Returns a non-zero value if the bit for the file descriptor fd is set in the file descriptor set pointed to by fdset, and 0 otherwise.
        if (FD_ISSET(listen_sock, &pool.ready_set) != 0) {
            addrlen = sizeof cli_addr;
	    // The accept function is called by a TCP server to return the next completed connection from the front of the completed connection queue.
            newfd = accept(listen_sock, (struct sockaddr *)&cli_addr, &addrlen);
            if (newfd == -1) {
                fprintf(stderr, "establishing new connection error\n");
                Log(fp, "Error happened when trying to establish a new client connection.\n");
                break;
            }
            else {
                /* add new client to pool */
		// add a new client to the pool and update pool attributes 
                add_client(newfd, &pool, (struct sockaddr_in *) &cli_addr);
            }
        }
        handle_clients(&pool);
    }

    close(listen_sock);

    return EXIT_SUCCESS;
}

int close_client_socket(int id, pool *p)
{
    FD_CLR(p->clientfd[id], &p->read_set);
    if (close(p->clientfd[id]) < 0)
    {
        Log(fp, "Failed closing socket.\n");
        return 1;
    }
    Log(fp, "Close connection to client on socket: %d.\n", p->clientfd[id]);
    p->clientfd[id] = -1;
    return 0;
}

int  open_listen_socket(int port) {
    int sock;
    struct sockaddr_in addr;

    int yes = 1;

    /* create a socket */
    // int sockfd = socket(domain, type, protocol)
    // domain: integer, communication domain e.g., AF_INET (IPv4 protocol) , AF_INET6 (IPv6 protocol)
    // type: communication type
    // SOCK_STREAM: TCP(reliable, connection oriented)
    // SOCK_DGRAM: UDP(unreliable, connectionless)
    // protocol: Protocol value for Internet Protocol(IP), which is 0. This is the same number which appears on protocol field in the IP header of a packet.(man protocols for more details)

    // So the correct thing to do is to use AF_INET in your struct sockaddr_in and PF_INET in your call to socket()
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        Log(fp, "Fail to create socket\n");
        return EXIT_FAILURE;
    }

    // This helps in manipulating options for the socket referred by the file descriptor sockfd. 
    // This is completely optional, but it helps in reuse of address and port. Prevents error such as: “address already in use”.
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        Log(fp, "Fail to set socketopt\n");
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* servers bind sockets to ports---notify the OS they accept connections */
    // The bind function assigns a local protocol address to a socket
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(sock);
        Log(fp, "Fail to bind socket\n");
        return EXIT_FAILURE;
    }
    // The listen function is called only by a TCP server to listen for the client request
    if (listen(sock, LISTENQ) < 0) {
        close(sock);
        Log(fp, "Fail to listen on socket\n");
        return EXIT_FAILURE;
    }
    return sock;
}

void init_pool(int listenfd, pool *p)
{
    int i;
    p->maxi = -1;
    for (i = 0; i < FD_SETSIZE; i++) {
        p->clientfd[i] = -1;
    }
    p->maxfd = listenfd;
    // Initializes the file descriptor set fdset to have zero bits for all file descriptors
    FD_ZERO(&p->read_set);
    // Sets the bit for the file descriptor fd in the file descriptor set fdset
    FD_SET(listenfd, &p->read_set);
}

// add a new client to the pool and update pool attributes         
// parameters: client_fd - the descriptor of new client                        
//             p    - pointer to pool instance                                 
// return:     0 on success, -1 on failure                                     
void add_client(int client_socket, pool *p, struct sockaddr_in *cli_addr) {
    int i;
    p->nready--;

    // only accept FD_SETSIZE - 5 clients to keep server from overloading
    for (i = 0; i < (FD_SETSIZE - 5); i++) {
        if (p->clientfd[i] < 0) {
            // add client descriptor to the pool
            p->clientfd[i] = client_socket;
	    // add the descriptor to the descriptor set
            FD_SET(client_socket, &p->read_set);
	    // update max descriptor and pool highwater mark
            if (client_socket > p->maxfd)
                p->maxfd = client_socket;
            if (i > p->maxi)
                p->maxi = i;
            break;
        }
    }
}
                                                   
// purpose:    process the ready set from the set of descriptors               
// parameters: p - pointer to the pool instance                                
// return:     0 on success, -1 on failure                                     
void handle_clients(pool *p) {
    int i, curfd;
    for (i = 0; (i <= p->maxi) && (p->nready > 0); i++) {
        curfd = p->clientfd[i];
        if ((curfd > 0) && (FD_ISSET(curfd, &p->ready_set))) {
            p -> nready--;
            HTTPContext *context = (HTTPContext *)calloc(1, sizeof(HTTPContext));
            context->keep_alive = 1;
            context->is_valid = 1;
            process_request(i, p, context);
            if (!context->keep_alive) {
                close_client_socket(i, p);
            }
            free(context);
        }
    }
}

// purpose:    handle a single request and return responses                    
// parameters: id        - the index of the client in the pool                 
//             p         - a pointer of pool struct                            
//             is_closed - idicator if the transaction is closed 
void process_request(int i, pool *p, HTTPContext *context) {

    int nbytes;
    char filename[BUFF_SIZE];
    struct stat sbuf;
    char buf[BUFF_SIZE];
    int curfd = p->clientfd[i];
    // The recv function is used to receive data over stream sockets or CONNECTED datagram sockets
    // read data using ith socket and stored information in buffer
    nbytes = recv(curfd, buf, BUFF_SIZE, 0);
    if (nbytes == 0) {
        return;
    }
    if (nbytes < 0) {
        context->keep_alive = 0;
        Log(fp, "Error occurred when receiving data from client\n");
        serve_error_handler(curfd, context, "500", "Internal Server Error", "The server encountered unexpected error.");
        return;
    }
    Log(fp, "Server received %d bytes data on socket %d\n", (int)nbytes, curfd);
    // parser handle the grammar check in request data 
    Request *request = parse(buf, nbytes, context);
    if (!context->is_valid) {
        context->keep_alive = 0;
        serve_error_handler(curfd, context, "400", "Bad Request", "The request line and header has error");
    }
    strcpy(context->method, request->http_method);
    strcpy(context->version, request->http_version);
    strcpy(context->uri, request->http_uri);
    // check HTTP method (support GET, POST, HEAD now)
    if (!is_valid_method(request->http_method)) {
        context->keep_alive = 0;
        serve_error_handler(curfd, context, "501", "Not Implemented", "HTTP method is not implemented by the server");
        return;
    }
    // check HTTP version
    if (strcasecmp(context->version, "HTTP/1.1")) {
        context->keep_alive = 0;
        serve_error_handler(curfd, context, "505", "HTTP Version not supported", "The current http version HTTP/1.0 is not supported by Liso server");
        return;
    }

    // parse uri (get filename and parameters if any)
    parse_uri(p, request->http_uri, filename);

    strcpy(context->filename, filename);
    if (stat(context->filename, &sbuf) < 0) {
        context->keep_alive = 0;
        serve_error_handler(curfd, context, "404", "Not Found", "File not found on Liso Server");
        return;
    }

    // parse request headers
    if (parse_header(curfd, request, context) != 0) return;

    // send response
    serve_request_handler(curfd, context);

    return;
}

int is_valid_method(char *method) {
    if (!strcasecmp(method, "GET"))
        return 1;
    else if (!strcasecmp(method, "POST"))
        return 1;
    else if (!strcasecmp(method, "HEAD"))
        return 1;
    return 0;
}

void get_time(char *date) {
    struct tm tm;
    time_t now;
    now = time(0);
    tm = *gmtime(&now);
    strftime(date, DATE_SIZE, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

int parse_uri(pool *p, char *uri, char* filename) {
    strcpy(filename, p->www);
    if (uri[strlen(uri)-1] == '/') {
        strcat(filename, "index.html");
        return 0;
    }
    char *pt = uri;
    while (*pt == '.' || *pt == '/') {pt++;}
    strcat(filename, pt);
    return 0;
}

int parse_header(int socketfd, Request *request, HTTPContext *context) {
    char *value;
    if ((value = get_header_value_by_key("Connection", request)) != NULL) {
        if (strstr(value, "close")) {
            context->keep_alive = 0;
        }
    }
    return 0;
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

void get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".css"))
        strcpy(filetype, "text/css");
    else if (strstr(filename, ".js"))
        strcpy(filetype, "application/javascript");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else
        strcpy(filetype, "text/plain");
}

void serve_request_handler(int client_fd, HTTPContext *context)
{
    if (!strcasecmp(context->method, "GET"))
        serve_get_handler(client_fd, context);
    if (!strcasecmp(context->method, "POST"))
        serve_post_handler(client_fd, context);
    if (!strcasecmp(context->method, "HEAD"))
        serve_head_handler(client_fd, context);
}

void serve_get_handler(int client_fd, HTTPContext *context)
{
    serve_head_handler(client_fd, context);
    serve_body_handler(client_fd, context);
}

void serve_head_handler(int client_fd, HTTPContext *context)
{
    struct tm tm;
    struct stat sbuf;
    char buff[BUFF_SIZE];
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
    sprintf(buff, "HTTP/1.1 200 OK\r\n");
    sprintf(buff, "%sServer: Liso/1.0\r\n", buff);
    sprintf(buff, "%sDate: %s\r\n", buff, date);
    if (!context->keep_alive) sprintf(buff, "%sConnection: keep-alive\r\n", buff);
    sprintf(buff, "%sContent-Length: %lld\r\n", buff, sbuf.st_size);
    sprintf(buff, "%sContent-Type: %s\r\n", buff, filetype);
    sprintf(buff, "%sCache-Control: no-cache\r\n", buff);
    sprintf(buff, "%sLast-Modified: %s\r\n\r\n", buff, tbuf);
    send(client_fd, buff, strlen(buff), 0);
}

int serve_body_handler(int client_fd, HTTPContext *context)
{
    int fd, filesize;
    char *ptr;
    struct stat sbuf;
    if ((fd = open(context->filename, O_RDONLY, 0)) < 0)
    {
        Log(fp, "Error: Cann't open file \n");
        serve_error_handler(client_fd, context, "500", "Internal Server Error", "The server encountered an unexpected condition");
        return -1;
    }
    stat(context->filename, &sbuf);
    filesize = sbuf.st_size;
    ptr = mmap(0, filesize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    int nbytes;
    nbytes = send(client_fd, ptr, filesize, 0);
    munmap(ptr, filesize);
    return 0;
}

void serve_post_handler(int client_fd, HTTPContext *context)
{
    char buff[BUFF_SIZE];
    char date[DATE_SIZE];

    get_time(date);

    sprintf(buff, "HTTP/1.1 200 No Content\r\n");
    sprintf(buff, "%sServer: Liso/1.0\r\n", buff);
    sprintf(buff, "%sDate: %s\r\n", buff, date);
    if (!context->keep_alive) sprintf(buff, "%sConnection: close\r\n", buff);
    sprintf(buff, "%sContent-Length: 0\r\n", buff);
    sprintf(buff, "%sContent-Type: text/html\r\n\r\n", buff);
    send(client_fd, buff, strlen(buff), 0);
}

void serve_error_handler(int client_fd, HTTPContext *context, char *errnum, char *shortmsg, char *longmsg)
{
    char buff[BUFF_SIZE], body[BUFF_SIZE], date[BUFF_SIZE];

    // HTTP error response body
    sprintf(body, "<html><title>Server Error</title>");
    sprintf(body, "%s<body>\r\n", body);
    sprintf(body, "%sError %s -- %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<br><p>%s</p></body></html>\r\n", body, longmsg);

    get_time(date);

    // HTTP error response header
    sprintf(buff, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    sprintf(buff, "%sServer: Liso/1.0\r\n", buff);
    sprintf(buff, "%sDate: %s\r\n", buff, date);
    if (!context->keep_alive) sprintf(buff, "%sConnection: close\r\n", buff);
    sprintf(buff, "%sContent-type: text/html\r\n", buff);
    sprintf(buff, "%sContent-length: %d\r\n\r\n", buff, (int) strlen(body));

    send(client_fd, buff, strlen(buff), 0);
    send(client_fd, body, strlen(body), 0);
}
