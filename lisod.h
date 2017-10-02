#ifndef _LISO_H_
#define _LISO_H_

#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>
#include "log.h"
#include "parse.h"

#define STAGE_INIT             1000
#define STAGE_ERROR            1001
#define STAGE_CLOSE            1002

#define REQ_VALID               1
#define REQ_INVALID             0
#define REQ_PIPE                2

#define DATE_SIZE     35
#define FILETYPE_SIZE 15
#define BUFF_SIZE     8192
#define MIN_LINE      64

/* this data struture wraps some attributes used to manage a pool of connected 
 * clients. (originally from CSAPP)*/

typedef struct {
    int maxfd;                   // Largest descriptor in read_set
    int nready;			 // Number of ready descriptors from select
    int maxi;			 // Highwater index into client array
    int clientfd[FD_SETSIZE];    // Set of active client descriptors
    fd_set read_set;		 // Set of all active descriptors
    fd_set ready_set;		 // Subset of descriptors ready for reading
    char *www;			 // Set of active read buffers
} pool;

#endif

