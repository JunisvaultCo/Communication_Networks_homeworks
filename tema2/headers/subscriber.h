#include "common.h"
#include <math.h>

#define TYPE_INT 0
#define TYPE_SHORT_REAL 1
#define TYPE_FLOAT 2
#define TYPE_STRING 3

#define MAX_SUBSCRIBE_COMMAND 100
#define BUFFER_CLIENT 100

typedef struct subscriber_data
{
    char id[ID_LEN + 1];
    int port;
    int sd;
    struct sockaddr_in server;
    struct pollfd fd;
    // wait until we can send them
    queue<tcp_sub_msg> buffer_w; 
    deque<char> buffer_r;
} subscriber_data;