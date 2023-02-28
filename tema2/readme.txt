Plaiasu Iulia-Silvia
All the code is mine, besides the python code. This was the second homework
of the Communication Networks class.
In this homework I had to implement a server and clients. I had to reimplement
the TCP and UPD connections from scratch.

All the code that is in C++ is mine.


I preferred poll over select as
https://www.man7.org/linux/man-pages/man2/select.2.html says that select has
a limit of 1024 clients. Also, poll allows you to put a negative file descriptor
(which gets ignored). Then it is no longer necessary to modify the entire list
at every disconnect. It is possible to simply maintain a queue of empty places
that can be verified first before increasing the vector which contains file
descriptors. This deletes in O(1).



VERBOSE:
Is especially for debug, but can also be used for errors.


The general lines of the server implementation (similar to the client) are:
1. initialises miscelleanous (init_misc): makes getline non-blocking,
deactivates buffer.
2. initialises the socket that receives new tcp connections (init_tcp)
3. initialises the socket that awaits upd messages (init_udp)
4. runs until exit the next while: first, it verifies with getline
(which is no longer blocking), if a line in the command line was written
(enter was perssed). If it was, then it takes it and verifies if it's exit.
If it is, the server closes the sockets and deallocates the socket vector.
Otherwise, it continues with a call to poll, which awaits 0.1 seconds at
most (in order to appear as responsive as possible to the user keyboard
input) to see if an event happened.

Similarly, a TCP client:

1. initialises the socket that connects to the server and other things (init):
makes getline non-blocking, deactivates Nagle, deactivates the buffer.
2. runs until exit the next while: first, it verifies with getline, which is
no longer blocking, if a line was written in the command line (if an enter
was pressed). If that happened, it takes it and verifies:
2.1. If it's exit, then it closes the socket and finishes the while.
2.2. If it's subscribe.
2.3. If it's unsubscribe.
If it's neither, then it continues with a call to poll with a timeout of 0.1s.



Maintaining the server clients in memory:

The server saves two properties, which it uses in maps:
- ID
- file descriptor
Based on these fields, it creates reading buffers (based on file descriptor),
writing buffers (based on ID), topic list (based on ID). Also, the
id - file descriptor mapping is 1:1 or inexistent (the latter only if the
client has not sent yet an ID / a client with that ID disconnected and none
took its ID) and creates two maps fd_to_id si id_to_fd. For optimisation,
for a topic we have two sets (one sf = 1, the other sf = 0) with all IDs
that are subscribed, instead of making one with all the topics an ID is
subscribed to.

The first state of a client is the one in which the server will only know
it by file descriptor. This state usually doesn't last long, the client
connecting with an ID right away.

The second state will be when it is known by both a file descriptor and ID.
This state usually follows the first, unless the ID is already used.

The third state is produced when it is disconnected, but the server
maintains the list to which it is subscribed. Also, if the tag sf = 1
was used last time when subscribind, it continues to add in the buffer
udp messages.



Types of messages and connections in the application

Basically, the server is a middle-man between the "news letters" that
connect to the server through UDP and the subscribers connected through
TCP. In order to limit and organise the messages in these connections
there are several structs used, similar to other headers from the
OSI stack (they will be detailed below). For optimisation, we will use
__attribute__ ((packed)) in order for the processor not to send extra
data on the wire.


UDP connection -> server

We have the udp_news struct which structures what an udp client sends: 
typedef struct __attribute__ ((packed)) udp_news
{
    char s[TOPIC_LEN]; // topic
    char type; // type
    char bi[UDP_MSG_LEN]; // the message itself
} udp_news;

A length is not necessary. The datagrams are received in full, the
length can be taken from the UDP protocol itself through API.
The server never sends a message back. This is the only message in the
connection. 


server -> TCP connection

The server sends these datagrams farther, when it's possible,
checking the datagram topic and seeing if sf = 0 and sf = 1
for this topic. (Actually, it first puts the messages in a queue and takes
them out when possible).
Due to modifying the transport protocol (from UDP to TCP), there needs to
be a new header. This new header is the following:

typedef struct __attribute__ ((packed)) udp_news_wrapper
{
    int len; // the length of the structure, since u_n can not be complete
    struct in_addr addr; // the address of the "news letter"
    short port; // the port of the "news letter"
    udp_news u_n; // the message itself, as was sent by UDP
} udp_news_wrapper;

The server sends no more or less than len. The client is careful to read
only len bits, the dimension of the message being therefore adaptable.


TCP connection -> server

The TCP client needs to send several message types. It will always send
type first. There are three types which use two structs:

- TCP_CONNECT - uses tcp_con_msg
- TCP_SUB - uses tcp_sub_msg
- TCP_UNSUB - uses tcp_sub_msg

Structs:

typedef struct __attribute__ ((packed)) tcp_con_msg
{
    unsigned char type;
    char id[11];
} tcp_con_msg;

typedef struct __attribute__ ((packed)) tcp_sub_msg
{
    unsigned char type; // either TCP_SUB or TCP_UNSUB
    char topic[51];
    bool sf; // matters only for TCP_SUB
} tcp_sub_msg;

TCP_CONNECT:
It is the first message sent to the server. Contains the ID.

TCP_SUB:
It is the subscription message. Has topic and sf.

TCP_UNSUB:
Unsubscription message.

Security issues: if someone wants to modify messages:
1. Server-ul receives an unknown kind of message. Closes the connection
2. Server-ul receives a TCP_SUB or TCP_UNSUB before TCP_CONNECT: Closes the
connection.
3. Server-ul receives a topic / id without '\0': ignores.
4. Server-ul is careful not to delete an ID for a connection that didn't have
an ID.



Reading the buffer:
For the reading buffers, I used deque (double-ended queue):
- Deleting is O(1).
- If we don't have enough data to complete reading a message,
what was read is put back, first creating an auxiliary deque
with them and only puts them back if the message they are part of is not full.