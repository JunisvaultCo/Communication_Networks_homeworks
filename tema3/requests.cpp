#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <stdio.h>
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"

void compute_rest_request(char *host, char **cookies, int cookies_count,
                            char *auth, char *message, char* content_type,
                            char **body_data, int body_data_fields_count)
{
    char *line = (char *) calloc(LINELEN, sizeof(char));
    char *body_data_buffer = (char *) calloc(LINELEN, sizeof(char));
    // Add the host
    sprintf(line, "Host: %s", host);
    compute_message(message, line);
    // (optional): add cookies
    if (cookies != NULL) {
        line[0] = '\0';
        strcat(line, "Cookie: ");
        for (int i = 0; i < cookies_count; i++)
        {
            strcat(line, cookies[i]);
            if (i != cookies_count - 1)
                strcat(line, "; ");
        }
        compute_message(message, line);
    }
    // (optional): add authorization
    if (auth)
    {
        sprintf(line, "Authorization: Bearer %s", auth);
        compute_message(message, line);
    }
    if (body_data_fields_count != 0)
    {
        /* Add necessary headers (Content-Type and Content-Length are mandatory)
                in order to write Content-Length you must first compute the message size
        */
        for (int i = 0; i < body_data_fields_count; i++)
        {
            if (i != 0)
                strcat(body_data_buffer, "&");
            strcat(body_data_buffer, body_data[i]);
        }
        sprintf(line, "Content-Length: %ld", strlen(body_data_buffer));
        compute_message(message, line);
        sprintf(line, "Content-Type: %s", content_type);
        compute_message(message, line);
    }
    // Add new line before payload
    compute_message(message, "");
    
    compute_message(message, body_data_buffer);

    free(line);
    free(body_data_buffer);
}


char *compute_get_request(char *host, char *url, char *query_params,
                            char **cookies, int cookies_count, char *auth)
{
    char *message = (char *) calloc(BUFLEN, sizeof(char));
    char *line = (char *) calloc(LINELEN, sizeof(char));

    // Write the method name, URL, request params (if any) and protocol type
    if (query_params != NULL) {
        sprintf(line, "GET %s?%s HTTP/1.1", url, query_params);
    } else {
        sprintf(line, "GET %s HTTP/1.1", url);
    }

    compute_message(message, line);
    compute_rest_request(host, cookies, cookies_count, auth, message, NULL,
                        NULL, 0);

    free(line);
    return message;
}

char *compute_post_request(char *host, char *url, char* content_type,
                            char **body_data, int body_data_fields_count,
                            char **cookies, int cookies_count, char *auth)
{
    char *message = (char *) calloc(BUFLEN, sizeof(char));
    char *line = (char *) calloc(LINELEN, sizeof(char));

    // Write the method name, URL and protocol type
    sprintf(line, "POST %s HTTP/1.1", url);
    compute_message(message, line);

    compute_rest_request(host, cookies, cookies_count, auth, message,
                        content_type, body_data, body_data_fields_count);

    free(line);
    return message;
}

char *compute_del_request(char *host, char *url, char* content_type, char **body_data,
                            int body_data_fields_count, char **cookies, int cookies_count,
                            char *auth)
{
    char *message = (char *) calloc(BUFLEN, sizeof(char));
    char *line = (char *) calloc(LINELEN, sizeof(char));

    // Write the method name, URL and protocol type
    sprintf(line, "DELETE %s HTTP/1.1", url);
    compute_message(message, line);
    
    compute_rest_request(host, cookies, cookies_count, auth, message,
                        content_type, body_data, body_data_fields_count);

    free(line);
    return message;
}