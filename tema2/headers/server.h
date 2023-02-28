#include "common.h"
#define BACKLOG 1000
#define INITIAL_CON_COUNT 10
#define BUFFER_SERVER 100

typedef struct server_data
{
    int port;
    int sd_tcp;
    int sd_udp;
    struct sockaddr_in server;
    struct pollfd *fds;
    int nfds;
    int current_size;
    /* This queue is how we get amortized O(1) insertion and
     * deletion in fds. Technically, we can have O(n), but pretty
     * much only in at least n steps, since we are doubling the
     * size of fds only when we don't have enough space. This
     * basically is reduced to a constant multiplier of 2.
     * The trick is that poll allows for ignoring elements with
     * negative file descriptors. So when we delete one element
     * we just set its file descriptor to negative.
     */ 
    queue<int> empties;
    map<string, int> id_to_fd;
    map<int, string> fd_to_id;
    map<int, deque<char>> r_buffer_for_fd;
    map<string, queue<udp_news_wrapper>> w_buffer_for_id;
    map<int, sockaddr_in> addr_for_fd;
    /* [sf], where [0] is for sf == 0, and [1] is for sf == 1.
     * this maps for the id, so that they can still be stored
     * in case of sf == 1. The alternative would have been
     * mapping based on fd, but that would have to change each
     * client restart.
     */
    map<string, set<string> > topic_subscribers[2];
} server_data;