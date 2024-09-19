#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>

typedef struct {
    char* method;
    char* path;
    char* version;
    char* headers;
    char* body;
} Request;

int server_socket_id;
int client_socket_id;
char* request_buffer;
Request* request;

void free_request(Request* request) {
    free(request->method);
    free(request->path);
    free(request->version);
    free(request->headers);
    free(request->body);
    free(request);
}

void clear_memory() {
    close(server_socket_id);
    close(client_socket_id);

    if (request_buffer != NULL) {
        free(request_buffer);
    }
    if (request != NULL) {
        free_request(request);
    }
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        printf("Received SIGINT, proceed to shutting down application\n");
        clear_memory();
        exit(0);
    }
}

struct sockaddr_in* create_server(
    int port,
    int backlog
) {
    struct sockaddr_in* server_address = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
    server_address->sin_family = AF_INET;
    server_address->sin_port = htons(port);
    server_address->sin_addr.s_addr = INADDR_ANY;

    server_socket_id = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_id < 0) {
        clear_memory();
        perror("Error creating socket");
        exit(1);
    }

    int res_setsockopt = setsockopt(
        server_socket_id, 
        SOL_SOCKET, 
        SO_REUSEADDR, 
        &(int){1}, 
        sizeof(int));
    if (res_setsockopt < 0) {
        clear_memory();
        perror("Error setting socket options");
        exit(1);
    }

    int res_bind = bind(
        server_socket_id, 
        (struct sockaddr*)server_address, 
        sizeof(*server_address));
    if (res_bind < 0) {
        clear_memory();
        perror("Error binding socket");
        exit(1);
    }

    int res_listen = listen(server_socket_id, backlog);
    if (res_listen < 0) {
        clear_memory();
        perror("Error listening on socket");
        exit(1);
    }

    return server_address;
}

char* copy_str(const char* start, int length) {
    char* result = (char*)malloc(length + 1);
    strncpy(result, start, length);
    result[length] = '\0';
    return result;
}

int parse_request(char* request_buffer, Request* request) {
    const char* method_endptr = strchr(request_buffer, ' ');
    if (method_endptr == NULL) {
        return -1;
    }
    request->method = copy_str(
        request_buffer, 
        method_endptr - request_buffer);

    const char* path_endptr = strchr(method_endptr + 1, ' ');
    if (path_endptr == NULL) {
        return -1;
    }
    request->path = copy_str(
        method_endptr + 1, 
        path_endptr - method_endptr - 1);
    
    const char* version_endptr = strstr(path_endptr + 1, "\r\n");
    if (version_endptr == NULL) {
        return -1;
    }
    request->version = copy_str(
        path_endptr + 1, 
        version_endptr - path_endptr - 1);
    
    const char* headers_startptr = version_endptr + 2;
    const char* headers_endptr = strstr(headers_startptr, "\r\n\r\n");
    if (headers_endptr == NULL) {
        request->headers = copy_str(
            headers_startptr, 
            strlen(headers_startptr));
        request->body = "";
    } else {
        request->headers = copy_str(
            headers_startptr, 
            headers_endptr - headers_startptr);
        request->body = copy_str(
            headers_endptr + 4, 
            strlen(headers_endptr + 4));
    }

    return 0;
}

void get_file_path(char* path, char* file_path) {
    // remove parameters
    char* params_start = strchr(path, '?');
    if (params_start != NULL) {
        *params_start = '\0';
    }

    if (path[strlen(path) - 1] == '/') {
        strcat(path, "index.html");
    }

    strcpy(file_path, "public");
    strcat(file_path, path);
}

void get_mime_type(char* file_path, char* mime_type) {
    char* extension = strrchr(file_path, '.');
    if (extension == NULL) {
        strcpy(mime_type, "text/plain");
        return;
    }

    if (strcmp(extension, ".html") == 0) {
        strcpy(mime_type, "text/html");
    } else if (strcmp(extension, ".css") == 0) {
        strcpy(mime_type, "text/css");
    } else if (strcmp(extension, ".js") == 0) {
        strcpy(mime_type, "text/javascript");
    } else if (strcmp(extension, ".jpg") == 0) {
        strcpy(mime_type, "image/jpeg");
    } else if (strcmp(extension, ".png") == 0) {
        strcpy(mime_type, "image/png");
    } else if (strcmp(extension, ".gif") == 0) {
        strcpy(mime_type, "image/gif");
    } else {
        strcpy(mime_type, "text/plain");
    }
}

int handle_get_request(Request* request) {
    char file_path[100];
    get_file_path(request->path, file_path);

    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        return -1;
    }

    char response_header[100];
    char mime_type[20];
    get_mime_type(file_path, mime_type);

    sprintf(response_header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\n\r\n", mime_type);
    int header_length = strlen(response_header);

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* response = (char*)malloc(header_length + file_size);
    memcpy(response, response_header, header_length);

    fread(response + header_length, 1, file_size, file);
    fclose(file);

    write(client_socket_id, response, header_length + file_size);
    free(response);    

    return 0;
}

int handle_request(Request* request) {
    if (strcmp(request->method, "GET") == 0) {
        return handle_get_request(request);
    }

    return -1;
}

int main(int argc, char *argv[]) {
    printf("Starting server\n");

    const int port = atoi(argv[1]);
    const int backlog = atoi(argv[2]);
    const int buffer_size = atoi(argv[3]);

    signal(SIGINT, handle_signal);

    struct sockaddr_in* server_address = create_server(port, backlog);

    char* address = inet_ntoa(server_address->sin_addr);
    printf("Listening on %s:%d\n", address, port);

    while (1) {
        request_buffer = (char*)malloc(buffer_size);
        client_socket_id = accept(server_socket_id, NULL, NULL);
        if (client_socket_id < 0) {
            clear_memory();
            perror("Error accepting connection");
            exit(1);
        }

        int res_read = read(client_socket_id, request_buffer, buffer_size);
        if (res_read < 0) {
            clear_memory();
            perror("Error reading from socket");
            exit(1);
        }

        request = (Request*)malloc(sizeof(Request));
        int res_parse_request = parse_request(request_buffer, request);
        if (res_parse_request < 0) {
            printf("Request not parsed\n");
            write(client_socket_id, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", 46);
            close(client_socket_id);
            continue;
        }
        free(request_buffer);

        int res_handle_request = handle_request(request);
        if (res_handle_request < 0) {
            printf("Request not handled\n");
            write(client_socket_id, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", 46);
            close(client_socket_id);
            free_request(request);
            continue;
        }

        printf("Closing socket\n");
        close(client_socket_id);
        free_request(request);
    }

    return 0;
}
