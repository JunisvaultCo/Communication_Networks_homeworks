#include "headers/common.h"

int turn_off_Neagle(int fd)
{
    int truee;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char *) &truee, 
                    sizeof(int));
}

void read_data(deque<char> &q, char *a, long size, deque<char> &extra)
{
    for (int i = 0; i < size; i++)
    {
        a[i] = q.front();
        extra.push_back(q.front());
        q.pop_front();
    }
}


void place_back(deque<char> &q, deque<char> &extra)
{
    while (!extra.empty())
    {
        q.push_front(extra.back());
        extra.pop_back();
    }
}