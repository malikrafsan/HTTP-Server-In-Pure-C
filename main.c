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
    char* data;
    int length;
} Bytes;

typedef struct {
    char* key;
    char* value;
} Header;

typedef struct {
    Header* arr_header;
    int headers_count;
} Headers;

typedef struct {
    char* method;
    char* path;
    char* version;
    Headers headers;
    char* body;
} Request;

typedef struct {
    char* version;
    char* status;
    Headers headers;
    Bytes body;
} Response;

int server_socket_id;
int client_socket_id;
char* request_buffer;
Request* request;
Response* response;

void free_bytes(Bytes* bytes) {
    if (bytes->data != NULL) {
        free(bytes->data);
    }
}

void free_request(Request* request) {
    free(request->method);
    free(request->path);
    free(request->version);
    for (int i = 0; i < request->headers.headers_count; i++) {
        free(request->headers.arr_header[i].key);
        free(request->headers.arr_header[i].value);
    }
    free(request->headers.arr_header);
    free(request->body);
    free(request);
}

void free_response(Response* response) {
    free(response->headers.arr_header);
    free_bytes(&response->body);
    free(response);
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
    char* headers;
    if (headers_endptr == NULL) {
        headers = copy_str(
            headers_startptr, 
            strlen(headers_startptr));
        request->body = "";
    } else {
        headers = copy_str(
            headers_startptr, 
            headers_endptr - headers_startptr);
        request->body = copy_str(
            headers_endptr + 4, 
            strlen(headers_endptr + 4));
    }

    int headers_count = 0;
    const char* header_startptr = headers;
    while (header_startptr != NULL) {
        const char* header_endptr = strstr(header_startptr, "\r\n");
        if (header_endptr == NULL) {
            break;
        }

        headers_count++;
        header_startptr = header_endptr + 2;
    }

    request->headers.headers_count = headers_count;
    request->headers.arr_header = (Header*)malloc(headers_count * sizeof(Header));
    header_startptr = headers;
    for (int i = 0; i < headers_count; i++) {
        const char* header_endptr = strstr(header_startptr, "\r\n");
        const char* colon_ptr = strchr(header_startptr, ':');
        if (colon_ptr == NULL) {
            return -1;
        }

        request->headers.arr_header[i].key = copy_str(
            header_startptr, 
            colon_ptr - header_startptr);
        request->headers.arr_header[i].value = copy_str(
            colon_ptr + 2, 
            header_endptr - colon_ptr - 2);

        header_startptr = header_endptr + 2;
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

void send_response(Response* response) {
    char response_header[1024];
    sprintf(response_header, "%s %s\r\n", response->version, response->status);

    for (int i = 0; i < response->headers.headers_count; i++) {
        strcat(response_header, response->headers.arr_header[i].key);
        strcat(response_header, ": ");
        strcat(response_header, response->headers.arr_header[i].value);
        strcat(response_header, "\r\n");
    }
    strcat(response_header, "\r\n");

    write(client_socket_id, response_header, strlen(response_header));
    if (response->body.length != 0) {
        write(client_socket_id, response->body.data, response->body.length);
    }
}

int handle_get_request(Request* request, Response* response) {
    char file_path[100];
    get_file_path(request->path, file_path);

    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        return -1;
    }

    char mime_type[20];
    get_mime_type(file_path, mime_type);

    response->version = "HTTP/1.1";
    response->status = "200 OK";
    response->headers.headers_count = 1;
    response->headers.arr_header = (Header*)malloc(sizeof(Header) * response->headers.headers_count);
    response->headers.arr_header[0].key = "Content-Type";
    response->headers.arr_header[0].value = mime_type;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    response->body.data = (char*)malloc(file_size);
    response->body.length = file_size;
    fread(response->body.data, 1, file_size, file);
    fclose(file);

    return 0;
}

int handle_request(Request* request, Response* response) {
    if (strcmp(request->method, "GET") == 0) {
        return handle_get_request(request, response);
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
        response = (Response*)malloc(sizeof(Response));

        int res_parse_request = parse_request(request_buffer, request);
        if (res_parse_request < 0) {
            printf("Request not parsed\n");

            response->version = "HTTP/1.1";
            response->status = "400 Bad Request";
            response->headers.headers_count = 1;
            response->headers.arr_header = (Header*)malloc(sizeof(Header) * response->headers.headers_count);
            response->headers.arr_header[0].key = "Content-Length";
            response->headers.arr_header[0].value = "0";

            send_response(response);
            close(client_socket_id);
            free_request(request);
            continue;
        }
        free(request_buffer);

        int res_handle_request = handle_request(request, response);
        if (res_handle_request < 0) {
            printf("Request not handled\n");

            response->version = "HTTP/1.1";
            response->status = "400 Bad Request";
            response->headers.headers_count = 1;
            response->headers.arr_header = (Header*)malloc(sizeof(Header) * response->headers.headers_count);
            response->headers.arr_header[0].key = "Content-Length";
            response->headers.arr_header[0].value = "0";

            send_response(response);
            close(client_socket_id);
            free_request(request);
            continue;
        }

        send_response(response);

        close(client_socket_id);
        free_request(request);
        free_response(response);
    }

    return 0;
}
