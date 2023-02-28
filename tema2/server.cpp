#include "headers/server.h"

int init_tcp(server_data &server_d, char **argv)
{
    server_d.port = atoi(argv[1]);
    server_d.sd_tcp = socket(AF_INET, SOCK_STREAM, 0);

    if (server_d.sd_tcp < 0)
    {
        if (VERBOSE)
            printf("Error while creating socket\n");
        return -1;
    }
    server_d.server.sin_family = AF_INET;
    server_d.server.sin_port = htons(server_d.port);
    server_d.server.sin_addr.s_addr = INADDR_ANY;
    if (turn_off_Neagle(server_d.sd_tcp) < 0 && VERBOSE)
        printf("Couldn't turn off Neagle!\n");
    int error = bind(server_d.sd_tcp, (struct sockaddr*)&server_d.server,
        sizeof(struct sockaddr));
    if (error)
    {
        if (VERBOSE)
            printf("Failed binding to port %d!\n", server_d.port);
        return -1;
    }
    if (VERBOSE)
    {
        printf("Succesful binding at port %d!\n", server_d.port);
        printf("Now waiting for messages...\n");
    }
    error = listen(server_d.sd_tcp, BACKLOG);
    if (error)
    {
        if (VERBOSE)
            printf("Listen failed!\n");
        return -1;
    }
    
    server_d.fds = (struct pollfd*)
        malloc(INITIAL_CON_COUNT * sizeof(struct pollfd));
    if (server_d.fds == NULL)
    {
        if (VERBOSE)
            printf("Couldn't allocate memory for fds!\n");
        return -1;
    }

    server_d.fds[0].fd = server_d.sd_tcp;
    server_d.fds[0].events = POLLIN | POLLOUT | POLLERR;
    server_d.nfds = 2;
    server_d.current_size = INITIAL_CON_COUNT;

    return 0;
}

int init_udp(server_data &server_d, char **argv)
{
    server_d.sd_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_d.sd_udp < 0)
    {
        if (VERBOSE)
            printf("Couldn't create UDP socket!\n");
        return -1;
    }
    int error = bind(server_d.sd_udp, (struct sockaddr *)&server_d.server,
        sizeof(server_d.server));
    if (error < 0)
    {
        if (VERBOSE)
            printf("Couldn't bind UDP socket!\n");
        return -1;
    }
    server_d.fds[1].fd = server_d.sd_udp;
    server_d.fds[1].events = POLLIN | POLLOUT | POLLERR;
    return 0;
}

// anything else that needs to be initiated for the server
// than the connections themselves
void init_misc(server_data &s)
{
    // make getline never wait for an entire line
    int flags = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, flags | O_NONBLOCK);
    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    s.nfds = 2;
}

int add_con(server_data &s)
{
    struct sockaddr_in cli;
    unsigned int c_len = sizeof(cli);
    int fd = accept(s.sd_tcp, (struct sockaddr *) &cli, &c_len);
    if (fd < 0)
        return -1;
    s.addr_for_fd[fd] = cli;
    int index;
    if (s.nfds == s.current_size && s.empties.size() == 0)
    {
        s.current_size *= 2;
        s.fds = (struct pollfd *)realloc(s.fds, s.current_size * sizeof(struct pollfd));
        // oops... failed to reallocate for whatever reason... oh well,
        // that is how life is...
        if (s.fds == NULL)
        {
            if (VERBOSE)
                printf("Failed to allocate new array");
            return -1;
        }
    }
    if (!s.empties.empty())
    {
        index = s.empties.front();
        s.empties.pop();
    }
    else
    {
        index = s.nfds;
        s.nfds++;
    }
    s.fds[index].fd = fd;
    s.fds[index].events = POLLIN | POLLOUT | POLLERR;
    if (turn_off_Neagle(s.fds[index].fd) < 0 && VERBOSE)
        printf("Couldn't turn off Neagle!\n");
    return 0;
}

void disconnect(server_data &s, int i, bool delete_id)
{
    if (delete_id)
    {
        string id = s.fd_to_id[s.fds[i].fd];
        s.id_to_fd.erase(id);
        s.fd_to_id.erase(s.fds[i].fd);
    }
    close(s.fds[i].fd);
    s.fds[i].fd = -1;
    s.r_buffer_for_fd[s.fds[i].fd] = deque<char>();
}

bool has_terminator(char *a, int size)
{
    for (int i = 0; i < size; i++)
        if (a[i] == '\0')
            return true;
    return false;
}

void parse_tcp_buffer(server_data &s, int fd, int i)
{
    deque<char> extra;
    // first we want to see the type. It's most important.
    unsigned char type;
    read_data(s.r_buffer_for_fd[fd], (char *)&type, sizeof(type), extra);
    if (type == TCP_CONNECT)
    {
        // we always expect the id length to be ID_LEN + 1.
        // This shouldn't be too costly (ID_LEN + 1 is currently 11).
        if (s.r_buffer_for_fd[fd].size() < ID_LEN + 1)
        {
            place_back(s.r_buffer_for_fd[fd], extra);
            return;
        }
        else
        {
            char id[ID_LEN + 1];
            read_data(s.r_buffer_for_fd[fd], id, sizeof(id), extra);
            if (!has_terminator(id, ID_LEN + 1))
            {
                if (VERBOSE)
                    printf("ID has no string terminator! Ignoring...\n");
                disconnect(s, i, false);
            }
            string s_id = string(id);
            // We got our id. Now we need to see
            // if it already exists
            if (s.id_to_fd.find(s_id) != s.id_to_fd.end())
            {
                printf("Client %s already connected.\n", id);
                disconnect(s, i, false);
            }
            else
            {
                s.id_to_fd[s_id] = fd;
                s.fd_to_id[fd] = s_id;
                struct sockaddr_in cli = s.addr_for_fd[fd];
                printf("New client %s connected from %s:%d.\n", id,
                    inet_ntoa(cli.sin_addr), cli.sin_port);
            }
        }
    }
    else if (type == TCP_SUB)
    {
        if (s.r_buffer_for_fd[fd].size() < sizeof(tcp_sub_msg) - 1)
        {
            place_back(s.r_buffer_for_fd[fd], extra);
            return;
        }
        else
        {
            // Sending something else than id when it's required
            // means the connection messed up. Disconnect immediately.
            if (s.fd_to_id.find(fd) == s.fd_to_id.end())
            {
                disconnect(s, i, false);
                return;
            }
            char buffer[sizeof(tcp_sub_msg)];
            buffer[0] = type;
            read_data(s.r_buffer_for_fd[fd], buffer + 1, sizeof(tcp_sub_msg) - 1, extra);
            tcp_sub_msg msg = *(tcp_sub_msg *) &buffer;
            if (!has_terminator(msg.topic, TOPIC_LEN + 1))
            {
                if (VERBOSE)
                    printf("Topic has no string terminator! Ignoring...\n");
                disconnect(s, i, false);
            }
            int k = (msg.sf + 1) % 2; // we have to modify the other
            if (s.topic_subscribers[k][msg.topic].find(s.fd_to_id[fd])
                    != s.topic_subscribers[k][msg.topic].end())
                s.topic_subscribers[k][msg.topic].erase(s.fd_to_id[fd]);
            s.topic_subscribers[msg.sf][msg.topic].insert(s.fd_to_id[fd]);
            if (VERBOSE)
                printf("%s subscribed to %s with sf = %d\n",
                        s.fd_to_id[fd].c_str(), msg.topic, msg.sf);
        }
    }
    else if (type == TCP_UNSUB)
    {
        if (s.r_buffer_for_fd[fd].size() < sizeof(tcp_sub_msg) - 1)
        {
            place_back(s.r_buffer_for_fd[fd], extra);
            return;
        }
        else
        {
            // Sending something else than id when it's required
            // means the connection messed up. Disconnect immediately.
            if (s.fd_to_id.find(fd) == s.fd_to_id.end())
            {
                disconnect(s, i, false);
                return;
            }
            char buffer[sizeof(tcp_sub_msg)];
            buffer[0] = type;
            read_data(s.r_buffer_for_fd[fd], buffer + 1, sizeof(tcp_sub_msg) - 1, extra);
            tcp_sub_msg msg = *(tcp_sub_msg *) &buffer;
            if (!has_terminator(msg.topic, TOPIC_LEN + 1))
            {
                if (VERBOSE)
                    printf("Topic has no string terminator! Ignoring...\n");
                disconnect(s, i, false);
            }
            for (int k = 0; k < 2; k++)
                if (s.topic_subscribers[k][msg.topic].find(s.fd_to_id[fd])
                        != s.topic_subscribers[k][msg.topic].end())
                    s.topic_subscribers[k][msg.topic].erase(s.fd_to_id[fd]);
            if (VERBOSE)
                printf("%s unsubscribed from %s.\n", s.fd_to_id[fd].c_str(),
                        msg.topic);
        }
    }
    else
    {
        // The TCP client has sent a deeply invalid message. Close the
        // connection.
        disconnect(s, i, s.fd_to_id.find(fd) != s.fd_to_id.end());
    }
    if (!s.r_buffer_for_fd[fd].empty())
        parse_tcp_buffer(s, fd, i);
}

int main(int argn, char **argv)
{
    if (argn < 2)
    {
        if (VERBOSE)
        {
            printf("There has to be at least 1 parameter.\n");
            printf("Format: path/file_name PORT.\n");
            printf("Example: %s 65535\n", argv[0]);
        }
        return 0;
    }
    server_data server_d;
    init_misc(server_d);
    if (init_tcp(server_d, argv) < 0)
        return -1;
    if (init_udp(server_d, argv) < 0)
        return -1;
    string a;
    while (true)
    {
        getline(cin, a);
        if (a.compare("exit") == 0)
        {
            // this includes all sockets
            for (int i = 0; i < server_d.nfds; i++)
                close(server_d.fds[i].fd);
            break;
        }
        cin.clear();
        int error = poll(server_d.fds, server_d.nfds, POLL_TIMEOUT);
        if (error < 0 && VERBOSE)
            printf("Error occurred in poll!\n");
        else if (error == 0 && VERBOSE)
        {}
        else if (error > 0)
        {
            int prevv = server_d.nfds;
            for (int i = 0; i < prevv; i++)
            {
                // new connection from TCP
                if (server_d.fds[i].fd == server_d.sd_tcp)
                {
                    if ((server_d.fds[i].revents & POLLIN) == 0)
                        continue;
                    if (add_con(server_d) < 0)
                        return -1;
                }
                else if (server_d.fds[i].fd == server_d.sd_udp)
                {
                    if ((server_d.fds[i].revents & POLLIN) == 0)
                        continue;
                    struct sockaddr from;
                    udp_news u_n;
                    unsigned int len = sizeof(from);
                    int msg_len = recvfrom(server_d.sd_udp, &u_n, sizeof(u_n), 0, &from, &len);
                    if (msg_len == -1)
                    {
                        if (VERBOSE)
                            printf("Couldn't read from UDP socket!\n");
                        continue;
                    }

                    char topic[TOPIC_LEN + 1];
                    memcpy(&topic, &u_n.s, TOPIC_LEN);
                    topic[TOPIC_LEN] = '\0';
                    if (VERBOSE)
                        printf("Received topic %s\n", topic);
                    short port = ((sockaddr_in *)&from)->sin_port;
                    struct in_addr addr = ((sockaddr_in *)&from)->sin_addr;
                    udp_news_wrapper u_w = {msg_len + (int) WRAPPING_MATERIAL, addr, port, u_n};
                    string s_top = string(topic);
                    for (int k = 0; k < 2; k++)
                        for (auto i: server_d.topic_subscribers[k][topic])
                            if (k == 1 || server_d.id_to_fd.find(i) != server_d.id_to_fd.end())
                                server_d.w_buffer_for_id[i].push(u_w);
                }
                else
                {
                    char ch;
                    int alive = recv(server_d.fds[i].fd, &ch, 1, MSG_DONTWAIT | MSG_PEEK);
                    if (!alive)
                    {
                        server_d.empties.push(i);
                        if (server_d.fd_to_id.find(server_d.fds[i].fd) != server_d.fd_to_id.end())
                        {
                            printf("Client %s disconnected.\n", server_d.fd_to_id[server_d.fds[i].fd].c_str());
                            disconnect(server_d, i, true);
                        }
                        else
                            disconnect(server_d, i, false);
                        continue;
                    }
                    if ((server_d.fds[i].revents & POLLOUT) != 0 && server_d.fds[i].fd >= 0)
                    {
                        if (server_d.fd_to_id.find(server_d.fds[i].fd) == server_d.fd_to_id.end()) {}
                        else
                        {
                            string id = server_d.fd_to_id[server_d.fds[i].fd];
                            queue<udp_news_wrapper> buf_c = server_d.w_buffer_for_id[id];
                            if (buf_c.empty()) {}
                            else
                            {
                                udp_news_wrapper news = buf_c.front();
                                buf_c.pop();
                                server_d.w_buffer_for_id[id] = buf_c;
                                send(server_d.fds[i].fd, &news, news.len, 0);
                            }
                        }
                    }
                    if ((server_d.fds[i].revents & POLLIN) != 0)
                    {
                        /* I have to be super careful since in TCP the messages
                        * can be cropped or extra can be added.
                        * so I have to save some buffers in r_buffer_for_id.
                        * First we add the new elements and then just deal with
                        * them in another function.
                        */
                        char c[BUFFER_SERVER];
                        int len = recv(server_d.fds[i].fd, c, BUFFER_SERVER, 0);
                        if (len <= 0)
                            continue;
                        if (server_d.r_buffer_for_fd.find(server_d.fds[i].fd)
                            == server_d.r_buffer_for_fd.end())
                            server_d.r_buffer_for_fd[server_d.fds[i].fd] = deque<char>();
                        for (int j = 0; j < len; j++)
                            server_d.r_buffer_for_fd[server_d.fds[i].fd].push_back(c[j]);
                        parse_tcp_buffer(server_d, server_d.fds[i].fd, i);
                    }
                }
            }
        }
    }
}