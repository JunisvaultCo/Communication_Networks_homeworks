#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"
#include "json-develop/single_include/nlohmann/json.hpp"
#define DATA_FIELD_LENGTH 1000
#define DEBUG false

using namespace nlohmann;

char host_ip[] = "34.241.4.235";
char host[] = "34.241.4.235:8080";
char json_payload[] = "application/json";
int port = 8080;
    
struct client
{
    int sockfd;
    bool logged_in = false;

    char **cookies = NULL;
    int cookies_length = 0;
    char *jwt = NULL;
};

void init(client &c)
{
    c.sockfd = open_connection(host_ip, port, PF_INET, SOCK_STREAM, 0);
}

void trim(char *s)
{
    if (strlen(s) >= 1 && s[strlen(s) - 1] == '\n')
        s[strlen(s) - 1] = '\0';
    if (strlen(s) >= 1 && s[strlen(s) - 1] == '\r')
        s[strlen(s) - 1] = '\0';
}

bool read_field(const char *name, json &j)
{
    char dest[DATA_FIELD_LENGTH];
    printf("%s=", name);
    fgets(dest, DATA_FIELD_LENGTH, stdin);
    if (dest[strlen(dest) - 1] != '\n')
    {
        printf("%s too long!\n", name);
        return false;
    }
    trim(dest);
    j[name] = dest;
    return true;
}

// has to be freed!
char *to_cstring(const char *field, json &j)
{
    std::string jwt_s;
    j[field].get_to(jwt_s);
    char *s = (char *)malloc(strlen(jwt_s.c_str()));
    strcpy(s, jwt_s.c_str());
    return s;
}

json ask_user()
{
    json j;
    if (!read_field("username", j))
        return NULL;
    if (!read_field("password", j))
        return NULL;
    std::string name, pass;
    j["username"].get_to(name);
    j["password"].get_to(pass);
    if (name.find(' ') != name.npos)
    {
        printf("Error: Name can't contain spaces! Please try again!\n");
        j = NULL;
    }
    if (name.size() == 0)
    {
        printf("Error: Name is empty! Please try again!\n");
        j = NULL;
    }
    if (pass.find(' ') != pass.npos)
    {
        printf("Error: Password can't contain spaces! Please try again!\n");
        j = NULL;
    }
    if (pass.size() == 0)
    {
        printf("Error: Password is empty! Please try again!\n");
        j =  NULL;
    }
    return j;
}

void deal_with_error_code(char *response)
{
    char *js = basic_extract_json_response(response);
    if (js != NULL)
    {
        json j = json::parse(js);
        std::string error;
        j["error"].get_to(error);
        printf("Error: %s\n", error.c_str());
    }
    else
    {
        printf("Error:%s\n", get_response_body(response));
    }
}

bool has_set_cookie(char *response)
{
    char *p = strstr(response, "Set-Cookie: ");
    return p != NULL;
}

void set_cookie(char *response, client &c)
{
    char *p = strstr(response, "Set-Cookie: ");
    p += strlen("Set-Cookie: ");
    // we don't really care about the rest.
    char *end = strstr(p, ";");
    c.cookies = (char **)malloc(sizeof(char *));
    c.cookies[0] = (char *)malloc(end - p);
    strncpy(c.cookies[0], p, end - p);
    c.cookies[0][end - p] = '\0';
    c.cookies_length = 1;
}

// the response has to be freed
char *receive_certain_message(char *message, client &c)
{
    char *response;
    if (DEBUG)
        printf("MESSAGE: %s\n", message);
    do
    {
        usleep(100);
        send_to_server(c.sockfd, message);
        response = receive_from_server(c.sockfd);
        if (strlen(response) == 0)
        {
            free(response);
            close_connection(c.sockfd);
            init(c);
        }
        else
            break;
    } while (true);
    if (DEBUG)
        printf("RESPONSE: %s\n", response);
    return response;
}

void register_client(client &c)
{
    char *message;
    char *response;
    char url[] = "/api/v1/tema/auth/register";
    json u = ask_user();
    if (u == NULL)
        return;
    char *data = (char *)malloc(strlen(u.dump().c_str()));
    strcpy(data, u.dump().c_str());
    message = compute_post_request(host, url, json_payload, &data, 1,
            c.cookies, c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
        deal_with_error_code(response);
    else
        printf("User created successfuly!\n");
    free(message);
    free(response);
    free(data);
}

void login(client &c)
{
    if (c.logged_in)
    {
        printf("You are already logged in! Perform a logout!\n");
        return;
    }
    char *message;
    char *response;
    char url[] = "/api/v1/tema/auth/login";
    json u = ask_user();
    if (u == NULL)
        return;
    char *data = (char *)malloc(strlen(u.dump().c_str()));
    strcpy(data, u.dump().c_str());
    message = compute_post_request(host, url, json_payload, &data, 1,
            c.cookies, c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        if (has_set_cookie(response))
        {
            set_cookie(response, c);
            printf("Successfully logged in!\n");
            c.logged_in = true;
        }
        else
        {
            printf("Failed logging in!\n");
        }
    }
    free(data);
    free(message);
    free(response);
}

bool verify_library(client &c)
{
    if (!c.logged_in)
    {
        printf("You aren't logged in!\n");
        return false;
    }
    if (!c.jwt)
    {
        printf("You don't have access to the library!\n");
        return false;
    }
    return true;
}

void enter_library(client &c)
{
    if (!c.logged_in)
    {
        printf("You aren't logged in!\n");
        return;
    }
    if (c.jwt)
    {
        printf("You already have access to the library!\n");
        return;
    }
    char *message;
    char *response;
    char url[] = "/api/v1/tema/library/access";
    message = compute_get_request(host, url, NULL, c.cookies,
                    c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        json j = json::parse(basic_extract_json_response(response));
        c.jwt = to_cstring("token", j);
        printf("Successfully accessed library!\n");
    }
    free(message);
    free(response);
}

void get_books(client &c)
{
    if (!verify_library(c))
        return;
    char *message;
    char *response;
    char url[] = "/api/v1/tema/library/books";
    message = compute_get_request(host, url, NULL, c.cookies,
                c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        printf("List of books:\n");
        json j = json::parse(basic_extract_json_response(response));
        for (unsigned int i = 0; i < j.size(); i++)
        {
            auto elem = j.at(i);
            std::string name;
            int id;
            j.at(i)["title"].get_to(name);
            j.at(i)["id"].get_to(id);
            printf("ID: %d with title: %s\n", id, name.c_str());
        }
        printf("\n");
    }
    free(message);
    free(response);
}

int get_int_field(const char *field, json &j)
{
    std::string s;
    j[field].get_to(s);

    for (char k: s)
    {
        if (k < '0' || k > '9')
        {
            printf("%s must have only digits! Not sending!\n", field);
            return -1;
        }
    }
    j[field] = atoi(s.c_str());
    if (atoi(s.c_str()) < 0)
        printf("%s has to fit in int! Try < 10^9!\n", field);
    if (s.size() == 0)
    {
        j[field] = -1;
        printf("%s is empty!\n", field);
        return -1;
    }
    return atoi(s.c_str());
}

json ask_book_id()
{
    json j;
    read_field("id", j);
    int i = get_int_field("id", j);
    if (i < 0)
        return NULL;
    return j;
}

void get_book(client &c)
{
    if (!verify_library(c))
        return;
    char *message;
    char *response;
    char *url = (char *)malloc(DATA_FIELD_LENGTH);
    json j = ask_book_id();
    if (j == NULL)
        return;
    int id;
    j["id"].get_to(id);
    sprintf(url, "/api/v1/tema/library/books/%d", id);
    message = compute_get_request(host, url, NULL, c.cookies,
                    c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        json j = json::parse(basic_extract_json_response(response));
        std::string title, author, genre, publisher;
        int page_count;
        j.at(0)["title"].get_to(title);
        j.at(0)["author"].get_to(author);
        j.at(0)["genre"].get_to(genre);
        j.at(0)["publisher"].get_to(publisher);
        j.at(0)["page_count"].get_to(page_count);
        printf("Book %s by %s, genre %s, publisher %s, page count %d\n",
                title.c_str(), author.c_str(), genre.c_str(),
                publisher.c_str(), page_count);
    }
    free(url);
    free(message);
    free(response);
}

json ask_add_book()
{
    json j;
    if (!read_field("title", j))
        return NULL;
    if (!read_field("author", j))
        return NULL;
    if (!read_field("genre", j))
        return NULL;
    if (!read_field("publisher", j))
        return NULL;
    if (!read_field("page_count", j))
        return NULL;
    int i = get_int_field("page_count", j);
    j["page_count"] = i;
    if (i < 0)
        return NULL;
    return j;
}

void add_book(client &c)
{
    if (!verify_library(c))
        return;
    char *message;
    char *response;
    char url[] = "/api/v1/tema/library/books";
    json j = ask_add_book();
    if (j == NULL)
        return;
    char *data = (char *)malloc(strlen(j.dump().c_str()));
    strcpy(data, j.dump().c_str());
    message = compute_post_request(host, url, json_payload, &data, 1, c.cookies,
            c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        printf("Successfully added book!\n");
    }
    free(data);
    free(message);
    free(response);
}

void delete_book(client &c)
{
    if (!verify_library(c))
        return;
    char *message;
    char *response;
    char *url = (char *)malloc(DATA_FIELD_LENGTH);
    json j = ask_book_id();
    if (j == NULL)
        return;
    int id;
    j["id"].get_to(id);
    sprintf(url, "/api/v1/tema/library/books/%d", id);
    char *data = (char *)malloc(strlen(j.dump().c_str()));
    strcpy(data, j.dump().c_str());
    message = compute_del_request(host, url, json_payload, &data, 1,
            c.cookies, c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        printf("Successfully deleted book!\n");
    }
    free(url);
    free(data);
    free(message);
    free(response);
}

void logout(client &c)
{
    if (!c.logged_in)
    {
        printf("Log in you must, in order to log out!\n");
        return;
    }
    char *message;
    char *response;
    char url[] = "/api/v1/tema/auth/logout";
    message = compute_get_request(host, url, NULL, c.cookies,
            c.cookies_length, c.jwt);
    response = receive_certain_message(message, c);
    int response_code = get_response_code(response);
    printf("Response Code: %d\n", response_code);
    if (response_code >= 400)
    {
        deal_with_error_code(response);
    }
    else
    {
        if (c.jwt)
        {
            free(c.jwt);
            c.jwt = NULL;
        }
        for (int i = 0; i < c.cookies_length; i++)
            free(c.cookies[i]);
        free(c.cookies);
        c.cookies_length = 0;
        c.cookies = NULL;
        c.logged_in = false;
        printf("Logged out successfully!\n");
    }
    free(message);
    free(response);
}

void client_exit(client &c)
{
    close_connection(c.sockfd);
    if (c.cookies != NULL)
    {
        for (int i = 0; i < c.cookies_length; i++)
            free(c.cookies[i]);
        free(c.cookies);
        c.cookies = NULL;
        c.cookies_length = 0;
    }
    if (c.jwt != NULL)
    {
        free(c.jwt);
        c.jwt = NULL;
    }
    exit(0);
}

int main(int argc, char *argv[])
{
    client c;
    init(c);
    char command[505];

    while (true)
    {
        fgets(command, 505, stdin);
        trim(command);
        if (strcmp("register", command) == 0)
            register_client(c);
        else if (strcmp("login", command) == 0)
            login(c);
        else if (strcmp("enter_library", command) == 0)
            enter_library(c);
        else if (strcmp("get_books", command) == 0)
            get_books(c);
        else if (strcmp("get_book", command) == 0)
            get_book(c);
        else if (strcmp("add_book", command) == 0)
            add_book(c);
        else if (strcmp("delete_book", command) == 0)
            delete_book(c);
        else if (strcmp("logout", command) == 0)
            logout(c);
        else if (strcmp("exit", command) == 0)
            client_exit(c);
    }

    return 0;
}
