#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
/* I would largely prefer using poll since linux manual pages
 * say that select can monitor at most 1024 separate file descriptors.
 * I will use this for client for consistency's sake
 */
#include <poll.h>
#include <iostream>
#include <fcntl.h> // for making getline non-blocking
#include <unistd.h> // close
#include <queue>
#include <string.h> // memcpy
#include <map>
#include <netinet/tcp.h>
#include <set>
#define VERBOSE false
#define UDP_MSG_LEN 1500
#define TOPIC_LEN 50
#define POLL_TIMEOUT 100
#define ID_LEN 10

using namespace std;

typedef struct __attribute__ ((packed)) udp_news
{
    char s[TOPIC_LEN];
    char type;
    char bi[UDP_MSG_LEN];
} udp_news;

typedef struct __attribute__ ((packed)) udp_news_wrapper
{
    // The length of the WRAPPER + the length of the actual (that is used)
    // u_n.
    int len;
    struct in_addr addr;
    short port;
    udp_news u_n;
} udp_news_wrapper;

// this shall always be the size of the wrapper without
// the actual content (u_n)
#define WRAPPING_MATERIAL (sizeof(udp_news_wrapper) - sizeof(udp_news))


/* TCP clients will have 2 main message structs
 * (what they send):
 * tcp_msg and tcp_con_msg.
 * tcp_con_msg will have only type TCP_CONNECT
 * tcp_sub_msg will have types TCP_SUB, TCP_UNSUB
 */

// both of these hold type as first, so that the
// server when receiving them can check what is what.
typedef struct __attribute__ ((packed)) tcp_sub_msg
{
    unsigned char type; // can be either TCP_SUB or TCP_UNSUB
    char topic[TOPIC_LEN + 1];
    bool sf; // only relevant in case of a TCP_SUB
} tcp_sub_msg;


typedef struct __attribute__ ((packed)) tcp_con_msg
{
    unsigned char type; // can be only TCP_CONNECT
    char id[ID_LEN + 1];
} tcp_con_msg;

#define TCP_CONNECT 1
#define TCP_SUB 2
#define TCP_UNSUB 3

int turn_off_Neagle(int fd);
void read_data(deque<char> &q, char *a, long size, deque<char> &extra);
void place_back(deque<char> &q, deque<char> &extra);