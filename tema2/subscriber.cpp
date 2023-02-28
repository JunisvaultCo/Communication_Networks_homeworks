#include "headers/subscriber.h"

int init(subscriber_data &c_d, char ** argv)
{
    strcpy(c_d.id, argv[1]);

    c_d.sd = socket(AF_INET, SOCK_STREAM, 0);
    if (c_d.sd < 0)
    {
        if (VERBOSE)
            printf("Unable to create socket!\n");
        return -1;
    }
    c_d.port = atoi(argv[3]);
    c_d.server.sin_family = AF_INET;
    c_d.server.sin_port = htons(c_d.port);
    c_d.server.sin_addr.s_addr = inet_addr(argv[2]);

    int error = connect(c_d.sd, (struct sockaddr*)&c_d.server, sizeof(struct sockaddr));
    if (error)
    {
        if (VERBOSE)
            printf("Unable to connect to %s:%d!\n", argv[2], c_d.port);
        return -1;
    }
    if (VERBOSE)
        printf("Succesful connection at %s:%d!\n", argv[2], c_d.port);
    c_d.fd.fd = c_d.sd;
    c_d.fd.events = POLLIN | POLLOUT | POLLPRI | POLLERR;
    if (turn_off_Neagle(c_d.sd) < 0 && VERBOSE)
        printf("Couldn't turn off Neagle!\n");

    // make getline never wait for an entire line
    int flags = fcntl(0, F_GETFL, 0);
    fcntl(0, F_SETFL, flags | O_NONBLOCK);

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);
    return 0;
}

void print_subscribe_usage()
{
    if (VERBOSE)
    {
        printf("subscribe usage: subscribe topic sf.\n");
        printf("Topic can be at most 50 letters in length!\n");
        printf("sf can be either 0 or 1 and it means whether the server ");
        printf("stores your messages when you are away!\n");
    }
}

void print_unsubscribe_usage()
{
    if (VERBOSE)
    {
        printf("unsubscribe usage: unsubscribe topic.\n");
        printf("Topic can be at most 50 letters in length!\n");
    }
}

// returns -1 if the application should close
int deal_with_user_commands(subscriber_data &c_d)
{
    string a;
    getline(cin, a);
    if (a.compare("exit") == 0)
    {
        close(c_d.sd);
        return -1;
    }
    if (a.find("subscribe") == 0)
    {
        char copyy[MAX_SUBSCRIBE_COMMAND];
        // prevent silly buffer overflow...
        strncpy(copyy, a.c_str(), MAX_SUBSCRIBE_COMMAND);
        char *pointer = strtok(copyy, " \r\n");
        if (pointer == NULL)
        {
            print_subscribe_usage();
            return 0;
        }
        pointer = strtok(NULL, " \r\n");
        if (pointer == NULL)
        {
            print_subscribe_usage();
            return 0;
        }
        tcp_sub_msg msg;
        memcpy(msg.topic, pointer, 51);
        msg.type = TCP_SUB;
        bool found_end = false;
        for (int i = 0; i < 51; i++)
            found_end = found_end | (msg.topic[i] == '\0');
        if (!found_end)
        {
            print_subscribe_usage();
            return 0;
        }
        pointer = strtok(NULL, " \r\n");
        if (pointer == NULL)
        {
            print_subscribe_usage();
            return 0;
        }
        msg.sf = 0;
        if (pointer[0] == '1')
            msg.sf = 1;
        else if (pointer[0] != '0')
        {
            print_subscribe_usage();
            return 0;
        }
        c_d.buffer_w.push(msg);
    }
    else if (a.find("unsubscribe") != a.npos)
    {
        char copyy[MAX_SUBSCRIBE_COMMAND];
        // prevent silly buffer overflow...
        strncpy(copyy, a.c_str(), MAX_SUBSCRIBE_COMMAND);
        char *pointer = strtok(copyy, " \r\n");
        if (pointer == NULL)
        {
            print_unsubscribe_usage();
            return 0;
        }
        pointer = strtok(NULL, " \r\n");
        if (pointer == NULL)
        {
            print_unsubscribe_usage();
            return 0;
        }
        tcp_sub_msg msg;
        memcpy(msg.topic, pointer, 51);
        msg.type = TCP_UNSUB;
        bool found_end = false;
        for (int i = 0; i < 51; i++)
            found_end = found_end | (msg.topic[i] == '\0');
        if (!found_end)
        {
            print_unsubscribe_usage();
            return 0;
        }
        c_d.buffer_w.push(msg);
    }
    cin.clear();
    return 0;
}

void receive_msg(subscriber_data &c_d)
{
    int len;
    deque<char> extra;
    read_data(c_d.buffer_r, (char *) &len, sizeof(len), extra);
    // len also considers the length of itself...
    if (len - sizeof(len) > c_d.buffer_r.size())
    {
        place_back(c_d.buffer_r, extra);
        return;
    }
    struct in_addr addr;
    read_data(c_d.buffer_r, (char *) &addr, sizeof(addr), extra);
    short port;
    read_data(c_d.buffer_r, (char *) &port, sizeof(port), extra);
    udp_news u_n;
    read_data(c_d.buffer_r, (char *) &u_n, len - WRAPPING_MATERIAL, extra);
    char topic[TOPIC_LEN + 1];
    memcpy(topic, u_n.s, TOPIC_LEN);
    topic[TOPIC_LEN] = '\0';
    // now, to understanding the data...
    if (u_n.type == TYPE_INT)
    {
        bool sign = *(char *)u_n.bi;
        int data = ntohl(*(int *)(u_n.bi + 1));
        if (sign == 0)
            printf("%s:%hu - %s - INT - %u\n", inet_ntoa(addr), port, topic,
                    data);
        else
            printf("%s:%hu - %s - INT - -%u\n", inet_ntoa(addr), port, topic,
                    data);
    }
    else if (u_n.type == TYPE_SHORT_REAL)
    {
        unsigned short data = ntohs(*(short int *)u_n.bi);
        printf("%s:%hu - %s - SHORT_REAL - %f\n", inet_ntoa(addr), port,
                topic, data / 100.0);
    }
    else if (u_n.type == TYPE_FLOAT)
    {
        bool sign = *(char *)u_n.bi;
        int data1 = ntohl(*(int *)(u_n.bi + 1));
        unsigned char exp = *(char *)(u_n.bi + 5);
        int a = 0 + exp;
        float data = pow(10, -a) * data1;
        if (sign == 0)
            printf("%s:%hu - %s - FLOAT - %f\n", inet_ntoa(addr), port,
                    topic, data);
        else
            printf("%s:%hu - %s - FLOAT - -%f\n", inet_ntoa(addr), port,
                    topic, data);
    }
    else
    {
        char aux[UDP_MSG_LEN + 1];
        memcpy(aux, u_n.bi, UDP_MSG_LEN);
        aux[UDP_MSG_LEN] = '\0';
        printf("%s:%hu - %s - STRING - %s\n", inet_ntoa(addr), port, topic,
                aux);
    }
}

int main(int argn, char **argv)
{
    if (argn < 4)
    {
        if (VERBOSE)
        {
            printf("There has to be at least 3 parameters.\n");
            printf("Format: path/file_name ID IP_SERVER PORT_SERVER.\n");
            printf("Example: %s 5 127.0.0.1 65535\n", argv[0]);
        }
        return 0;
    }
    subscriber_data client_d;
    if (init(client_d, argv) < 0)
        return -1;
    bool sent_id = false;
    while (true)
    {
        if (deal_with_user_commands(client_d) < 0)
            break;
        int error = poll(&client_d.fd, 1, POLL_TIMEOUT);
        if (error < 0 && VERBOSE)
            printf("Error occurred in poll!\n");
        else if (error == 0 && VERBOSE)
        {}
        else if (error > 0)
        {
            char ch;
            int alive = recv(client_d.sd, &ch, 1, MSG_DONTWAIT | MSG_PEEK);
            if (!alive || (client_d.fd.revents & POLLERR) != 0)
            {
                if (VERBOSE)
                    printf("Server closed. Closing...\n");
                close(client_d.sd);
                break;
            }
            if ((client_d.fd.revents & POLLOUT) != 0)
            {
                if (!sent_id)
                {
                    tcp_con_msg msg;
                    strcpy(msg.id, client_d.id);
                    msg.type = TCP_CONNECT;
                    send(client_d.sd, &msg, sizeof(msg), 0);
                    sent_id = true;
                }
                else if (!client_d.buffer_w.empty())
                {
                    tcp_sub_msg msg = client_d.buffer_w.front();
                    client_d.buffer_w.pop();
                    int result = send(client_d.sd, &msg, sizeof(msg), 0);
                    if (result < 0)
                    {
                        if (VERBOSE)
                            printf("Couldn't send to server!\n");
                    }
                    else if (msg.type == TCP_SUB)
                       printf("Subscribed to topic.\n");
                    else
                       printf("Unsubscribed from topic.\n");
                }
            }
            if ((client_d.fd.revents & POLLIN) != 0)
            {
                char a[BUFFER_CLIENT];
                int len = recv(client_d.sd, a, BUFFER_CLIENT, 0);
                for (int i = 0; i < len; i++)
                    client_d.buffer_r.push_back(a[i]);
                if (len == 0)
                    continue;
                receive_msg(client_d);
            }
        }
    }
    fflush(stdout);
}