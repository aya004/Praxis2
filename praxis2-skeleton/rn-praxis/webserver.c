#include <assert.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "data.h"
#include "http.h"
#include "util.h"

#define MAX_RESOURCES 100


struct tuple resources[MAX_RESOURCES] = {
        {"/static/foo", "Foo", sizeof "Foo" - 1},
        {"/static/bar", "Bar", sizeof "Bar" - 1},
        {"/static/baz", "Baz", sizeof "Baz" - 1}
};

typedef struct Node{
    int id;
    char *ip;
    int port;
    struct Node* pred;
    struct Node* succ;
}__attribute__((packed)) Node;




typedef struct Message{
    uint8_t flag;
    uint16_t hash;
    uint16_t id;
    uint32_t ip;
    uint16_t port;
}__attribute__((packed)) Message;



/**
 * Derives a sockaddr_in structure from the provided host and port information.
 *
 * @param host The host (IP address or hostname) to be resolved into a network address.
 * @param port The port number to be converted into network byte order.
 *
 * @return A sockaddr_in structure representing the network address derived from the host and port.
 */
static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
            .ai_family = AF_INET,
    };
    struct addrinfo *result_info;

    // Resolve the host (IP address or hostname) into a list of possible addresses.
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error parsing host/port");
        exit(EXIT_FAILURE);
    }

    // Copy the sockaddr_in structure from the first address in the list
    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);

    // Free the allocated memory for the result_info
    freeaddrinfo(result_info);
    return result;
}

int udp_node_socket(struct sockaddr_in addr){
    const int enable = 1;


    // Create a socket
    int sockDgram = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockDgram == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Set the SO_REUSEADDR socket option to allow reuse of local addresses
    if (setsockopt(sockDgram, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind socket to the provided address
    if (bind(sockDgram, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sockDgram);
        exit(EXIT_FAILURE);
    }


    return sockDgram;
}



void lookup_reply(struct Node* node) {

    struct sockaddr_in serveraddr;
    char port[6];
    snprintf(port, sizeof(port), "%d", node->port);

    serveraddr = derive_sockaddr(node->ip, port);

    int sock = udp_node_socket(serveraddr);

    struct sockaddr_in clientaddr;
    socklen_t addr_len = sizeof(clientaddr);
    bzero(&clientaddr, sizeof(clientaddr));
    struct Message *msg = malloc(sizeof(struct Message));
    ssize_t recvm = recvfrom(sock, msg, sizeof(Message), 0, (struct sockaddr*)&clientaddr, &addr_len);
    if(recvm == -1){
        perror("recvfrom");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if(htons(msg->hash) > node->id && htons(msg->hash) <= node->succ->id){ //lookup_reply

        struct in_addr self_addr;
        inet_pton(AF_INET, node->succ->ip, &self_addr.s_addr);

        struct Message* reply = malloc(sizeof(Message));
        reply->flag = 1;
        reply->hash = htons(node->id);
        reply->id = htons(node->succ->id);
        reply->ip = self_addr.s_addr;
        reply->port = htons(node->succ->port);


        sendto(sock, reply, sizeof(Message), 0, (struct sockaddr*) &clientaddr, sizeof(clientaddr));

    }
    else{ //lookup_forward


        struct sockaddr_in succaddr;
        char succport[6];
        snprintf(succport, sizeof(succport), "%d", node->succ->port);

        succaddr = derive_sockaddr(node->succ->ip, succport);

        sendto(sock, msg, sizeof(Message), 0, (struct sockaddr*) &succaddr, sizeof(succaddr));


    }

}

void dht_reply(struct Node* node, int sock) {



    struct sockaddr_in clientaddr;
    socklen_t addr_len = sizeof(clientaddr);
    bzero(&clientaddr, sizeof(clientaddr));
    struct Message *msg = malloc(sizeof(struct Message));
    ssize_t recvm = recvfrom(sock, msg, sizeof(Message), 0, (struct sockaddr*)&clientaddr, &addr_len);
    if(recvm == -1){
        perror("recvfrom");
        close(sock);
        exit(EXIT_FAILURE);
    }
    struct Node* tmp = node;


    if(msg->hash > node->id && msg->hash <= node->succ->id){ //lookup_reply

        struct in_addr self_addr;
        inet_pton(AF_INET, node->succ->ip, &self_addr.s_addr);

        struct Message* reply = malloc(sizeof(Message));
        reply->flag = 1;
        reply->hash = htons(node->id);
        reply->id = htons(node->succ->id);
        reply->ip = self_addr.s_addr;
        reply->port = htons(node->succ->port);

        sendto(sock, reply, sizeof(Message), 0, (struct sockaddr*) &clientaddr, sizeof(clientaddr));
    }
    while(!(msg->hash > tmp->id && msg->hash <= tmp->succ->id)){ //lookup_forward

        struct sockaddr_in succaddr;
        char succport[6];
        snprintf(succport, sizeof(succport), "%d", tmp->succ->port);
        succaddr = derive_sockaddr(tmp->succ->ip, succport);
        sendto(sock, msg, sizeof(Message), 0, (struct sockaddr*) &succaddr, sizeof(succaddr));
        tmp = node->succ;

    }

}

int lookup_send(struct Node* node, unsigned short hash_value){
    struct sockaddr_in addr;
    int node_socket = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(node->succ->port);
    inet_pton(AF_INET, node->succ->ip, &addr.sin_addr);

    if(connect(node_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        printf("\n Error : Connect Failed \n");
        close(node_socket);
        exit(EXIT_FAILURE);
    }

    struct in_addr self_addr;
    inet_pton(AF_INET, node->ip, &self_addr.s_addr);

    struct Message* msg = malloc(sizeof(Message));
    msg->flag = htons(0);
    msg->hash = htons(hash_value);
    msg->id = htons(node->id);
    msg->ip = self_addr.s_addr;
    msg->port = htons(node->port);
    sendto(node_socket, msg, sizeof(Message), 0, (struct sockaddr*) &addr, sizeof(addr));
    return node_socket;

}

struct Node* reply_check(int sock){

    struct sockaddr_in clientaddr;
    socklen_t addr_len = sizeof(clientaddr);
    bzero(&clientaddr, sizeof(clientaddr));
    struct Message *msg = malloc(sizeof(struct Message));
    ssize_t recvm = recvfrom(sock, msg, sizeof(Message), 0, (struct sockaddr*)&clientaddr, &addr_len);
    if(recvm == -1){
        perror("recvfrom");
        close(sock);
        exit(EXIT_FAILURE);
    }
    struct Node* responsible = malloc(sizeof(Node));
    responsible->id = msg->id;
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &msg->ip, ip, INET_ADDRSTRLEN);
    responsible->ip = calloc(sizeof(ip), sizeof(char));
    memcpy(responsible->ip, ip, strlen(ip));
    responsible->port = ntohs(msg->port);
    return responsible;
}


/**
 * Sends an HTTP reply to the client based on the received request.
 *
 * @param conn      The file descriptor of the client connection socket.
 * @param request   A pointer to the struct containing the parsed request information.
 */
void send_reply(int conn, struct request* request, struct Node* node, unsigned short hash_value) {

    // Create a buffer to hold the HTTP reply
    char buffer[HTTP_MAX_SIZE];
    char *reply = buffer;
    char *rep = buffer;

    fprintf(stderr, "Handling %s request for %s (%lu byte payload)\n", request->method, request->uri, request->payload_length);

    if(node != NULL){
        if(node->pred->id == node->succ->id){
            if(hash_value > node->id && hash_value <= node->succ->id){
                sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n", (int) strlen(node->succ->ip), node->succ->ip, node->succ->port,(int) strlen(request->uri), request->uri);
            }else{
                reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            }
        }

        if(node->pred->id != node->succ->id){


            if (hash_value > node->id && hash_value > node->succ->id) {

                rep = "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nContent-Length: 0\r\n\r\n";
                send(conn, rep, strlen(rep), 0);

                int sock = lookup_send(node, hash_value);

                if(hash_value > node->succ->id && hash_value <= node->succ->succ->id){
                    struct sockaddr_in node_addr;
                    char node_port[6];
                    snprintf(node_port, sizeof(node_port), "%d", node->port);

                    node_addr = derive_sockaddr(node->ip, node_port);

                    int node_sock = udp_node_socket(node_addr);
                    struct Node *res = reply_check(node_sock);
                    sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n",
                            (int) strlen(res->ip), res->ip, res->port, (int) strlen(request->uri), request->uri);
                    send(conn, reply, strlen(reply), 0);
                }
                else{
                    reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                }
            } else{
                if (strcmp(request->method, "GET") == 0) {
                    if(htons(hash_value) > node->id && htons(hash_value) <= node->succ->id){
                        sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n", (int) strlen(node->succ->ip), node->succ->ip, node->succ->port,(int) strlen(request->uri), request->uri);

                    }

                    if((htons(hash_value) > node->id && htons(hash_value) > node->succ->id)){

                        struct sockaddr_in serveraddr;
                        char port[6];
                        snprintf(port, sizeof(port), "%d", node->succ->port);

                        serveraddr = derive_sockaddr(node->succ->ip, port);

                        int sock = udp_node_socket(serveraddr);

                        int node_socket = lookup_send(node, hash_value);


                        dht_reply(node->succ, sock);

                        struct Node* resp = reply_check(node_socket);

                        sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n",
                                (int) strlen(resp->ip), resp->ip, resp->port, (int) strlen(request->uri), request->uri);

                    }
                    if((htons(hash_value) < node->id && htons(hash_value) < node->succ->id)){
                        if(htons(hash_value) < node->pred->id){
                            sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n", (int) strlen(node->succ->ip), node->succ->ip, node->succ->port,(int) strlen(request->uri), request->uri);
                            send(conn, reply, strlen(reply), 0);
                        }
                        else{
                            sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n", (int) strlen(node->succ->ip), node->succ->ip, node->succ->port,(int) strlen(request->uri), request->uri);
                            send(conn, reply, strlen(reply), 0);
                        }
                    }
                    if((htons(hash_value) < node->id && htons(hash_value) > node->succ->id)){
                        sprintf(reply, "HTTP/1.1 303 See Other\r\nLocation: http://%.*s:%d%.*s\r\nContent-Length: 0\r\n\r\n", (int) strlen(node->succ->ip), node->succ->ip, node->succ->port,(int) strlen(request->uri), request->uri);
                        send(conn, reply, strlen(reply), 0);

                    }


                    // Find the resource with the given URI in the 'resources' array.


                    size_t resource_length;
                    const char* resource = get(request->uri, resources, MAX_RESOURCES, &resource_length);

                    if (resource) {
                        sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n%.*s", resource_length, (int) resource_length, resource);
                    } else {
                        reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
                    }
                } else if (strcmp(request->method, "PUT") == 0) {

                    // Try to set the requested resource with the given payload in the 'resources' array.
                    if (set(request->uri, request->payload, request->payload_length, resources, MAX_RESOURCES
                    )) {
                        reply = "HTTP/1.1 204 No Content\r\n\r\n";
                    } else {
                        reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
                    }
                }


                else if (strcmp(request->method, "DELETE") == 0) {

                    // Try to delete the requested resource from the 'resources' array
                    if (delete(request->uri, resources, MAX_RESOURCES)) {
                        reply = "HTTP/1.1 204 No Content\r\n\r\n";
                    } else {
                        reply = "HTTP/1.1 404 Not Found\r\n\r\n";
                    }
                }
            }



        }
    }
    else if (strcmp(request->method, "GET") == 0) {
        // Find the resource with the given URI in the 'resources' array.
        size_t resource_length;
        const char* resource = get(request->uri, resources, MAX_RESOURCES, &resource_length);

        if (resource) {
            sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n%.*s", resource_length, (int) resource_length, resource);
        } else {
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        }
    } else if (strcmp(request->method, "PUT") == 0) {
        // Try to set the requested resource with the given payload in the 'resources' array.
        if (set(request->uri, request->payload, request->payload_length, resources, MAX_RESOURCES
        )) {
            reply = "HTTP/1.1 204 No Content\r\n\r\n";
        } else {
            reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
        }
    } else if (strcmp(request->method, "DELETE") == 0) {
        // Try to delete the requested resource from the 'resources' array
        if (delete(request->uri, resources, MAX_RESOURCES)) {
            reply = "HTTP/1.1 204 No Content\r\n\r\n";
        } else {
            reply = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
    } else {
        reply = "HTTP/1.1 501 Method Not Supported\r\n\r\n";
    }

    // Send the reply back to the client
    if (send(conn, reply, strlen(reply), 0) == -1) {
        perror("send");
        close(conn);
    }
}


uint16_t hash(const char* str){ //Copied from Aufgabenblatt
    uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256((uint8_t *)str, strlen(str), digest);
    return htons(*((uint16_t *)digest)); // We only use the first two bytes here
}


/**
 * Processes an incoming packet from the client.
 *
 * @param conn The socket descriptor representing the connection to the client.
 * @param buffer A pointer to the incoming packet's buffer.
 * @param n The size of the incoming packet.
 *
 * @return Returns the number of bytes processed from the packet.
 *         If the packet is successfully processed and a reply is sent, the return value indicates the number of bytes processed.
 *         If the packet is malformed or an error occurs during processing, the return value is -1.
 *
 */
size_t process_packet(int conn, char* buffer, size_t n, struct Node* node) {
    struct request request = {
            .method = NULL,
            .uri = NULL,
            .payload = NULL,
            .payload_length = -1
    };
    ssize_t bytes_processed = parse_request(buffer, n, &request);

    if (bytes_processed > 0) {
        unsigned short hash_value;
        hash_value = hash(request.uri);
        send_reply(conn, &request, node, hash_value);

        // Check the "Connection" header in the request to determine if the connection should be kept alive or closed.
        const string connection_header = get_header(&request, "Connection");
        if (connection_header && strcmp(connection_header, "close")) {
            return -1;
        }
    } else if (bytes_processed == -1) {
        // If the request is malformed or an error occurs during processing, send a 400 Bad Request response to the client.
        const string bad_request = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(conn, bad_request, strlen(bad_request), 0);
        printf("Received malformed request, terminating connection.\n");
        close(conn);
        return -1;
    }

    return bytes_processed;
}

/**
 * Sets up the connection state for a new socket connection.
 *
 * @param state A pointer to the connection_state structure to be initialized.
 * @param sock The socket descriptor representing the new connection.
 *
 */
static void connection_setup(struct connection_state* state, int sock) {
    // Set the socket descriptor for the new connection in the connection_state structure.
    state->sock = sock;

    // Set the 'end' pointer of the state to the beginning of the buffer.
    state->end = state->buffer;

    // Clear the buffer by filling it with zeros to avoid any stale data.
    memset(state->buffer, 0, HTTP_MAX_SIZE);
}


/**
 * Discards the front of a buffer
 *
 * @param buffer A pointer to the buffer to be modified.
 * @param discard The number of bytes to drop from the front of the buffer.
 * @param keep The number of bytes that should be kept after the discarded bytes.
 *
 * @return Returns a pointer to the first unused byte in the buffer after the discard.
 * @example buffer_discard(ABCDEF0000, 4, 2):
 *          ABCDEF0000 ->  EFCDEF0000 -> EF00000000, returns pointer to first 0.
 */
char* buffer_discard(char* buffer, size_t discard, size_t keep) {
    memmove(buffer, buffer + discard, keep);
    memset(buffer + keep, 0, discard);  // invalidate buffer
    return buffer + keep;
}

/**
 * Handles incoming connections and processes data received over the socket.
 *
 * @param state A pointer to the connection_state structure containing the connection state.
 * @return Returns true if the connection and data processing were successful, false otherwise.
 *         If an error occurs while receiving data from the socket, the function exits the program.
 */
bool handle_connection(struct connection_state* state, struct  Node* node) {
    // Calculate the pointer to the end of the buffer to avoid buffer overflow
    const char* buffer_end = state->buffer + HTTP_MAX_SIZE;

    // Check if an error occurred while receiving data from the socket
    ssize_t bytes_read = recv(state->sock, state->end, buffer_end - state->end, 0);
    if (bytes_read == -1) {
        perror("recv");
        close(state->sock);
        exit(EXIT_FAILURE);
    } else if (bytes_read == 0) {
        return false;
    }

    char* window_start = state->buffer;
    char* window_end = state->end + bytes_read;

    ssize_t bytes_processed = 0;
    while((bytes_processed = process_packet(state->sock, window_start, window_end - window_start, node)) > 0) {
        window_start += bytes_processed;
    }
    if (bytes_processed == -1) {
        return false;
    }

    state->end = buffer_discard(state->buffer, window_start - state->buffer, window_end - window_start);
    return true;
}


Node* initialize(char* ip, int port, int id){
    Node* node = (Node*)malloc(sizeof(Node));
    if(node != NULL){
        node->ip = calloc(sizeof(ip), sizeof(char));
        memcpy(node->ip, ip, strlen(ip));
        node->port = port;
        node->id = id;
        node->pred = NULL;
        node->succ = NULL;
    }
    return node;
}


/**
 * Sets up a TCP server socket and binds it to the provided sockaddr_in address.
 *
 * @param addr The sockaddr_in structure representing the IP address and port of the server.
 *
 * @return The file descriptor of the created TCP server socket.
 */
static int setup_server_socket(struct sockaddr_in addr) {
    const int enable = 1;
    const int backlog = 1;

    // Create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Avoid dead lock on connections that are dropped after poll returns but before accept is called
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    // Set the SO_REUSEADDR socket option to allow reuse of local addresses
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind socket to the provided address
    if (bind(sock, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Start listening on the socket with maximum backlog of 1 pending connection
    if (listen(sock, backlog)) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return sock;
}


/**
*  The program expects 3; otherwise, it returns EXIT_FAILURE.
*
*  Call as:
*
*  ./build/webserver self.ip self.port
*/
int main(int argc, char** argv) {

    if (argc < 3) {
        return EXIT_FAILURE;
    }

    struct connection_state state = {0};

    struct sockaddr_in addr = derive_sockaddr(argv[1], argv[2]);

    // Set up a server socket.
    int server_socket = setup_server_socket(addr);

    Node* node;
    if(argc == 4) {
        node = initialize(argv[1], atoi(argv[2]), atoi(argv[3]));
        Node* succ = initialize(getenv("SUCC_IP"), atoi(getenv("SUCC_PORT")), atoi(getenv("SUCC_ID")));
        Node* pred = initialize(getenv("PRED_IP"), atoi(getenv("PRED_PORT")), atoi(getenv("PRED_ID")));
        pred->pred= succ;
        succ->succ = pred;
        node->succ = succ;
        node->pred = pred;

        if(node->pred->id == 0){
            lookup_reply(node);
        }

    }
    if(argc == 3){
        node = NULL;
    }

    //udp_socket
    int sockDgram = udp_node_socket(addr);

    // Create an array of pollfd structures to monitor sockets.
    struct pollfd sockets[2] = {
            { .fd = server_socket, .events = POLLIN },
    };

    while (true) {


        // Use poll() to wait for events on the monitored sockets.
        int ready = poll(sockets, sizeof(sockets) / sizeof(sockets[0]), -1);
        if (ready == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Process events on the monitored sockets.
        for (size_t i = 0; i < sizeof(sockets) / sizeof(sockets[0]); i += 1) {
            if (sockets[i].revents != POLLIN) {
                // If there are no POLLIN events on the socket, continue to the next iteration.
                continue;
            }
            int s = sockets[i].fd;

            if (s == server_socket) {

                // If the event is on the server_socket, accept a new connection from a client.
                int connection = accept(server_socket, NULL, NULL);
                if (connection == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                    close(server_socket);
                    perror("accept");
                    exit(EXIT_FAILURE);
                } else {
                    connection_setup(&state, connection);

                    // limit to one connection at a time
                    sockets[0].events = 0;
                    sockets[1].fd = connection;
                    sockets[1].events = POLLIN;
                }
            } else {
                assert(s == state.sock);

                // Call the 'handle_connection' function to process the incoming data on the socket.
                bool cont = handle_connection(&state, node);
                if (!cont) {  // get ready for a new connection
                    sockets[0].events = POLLIN;
                    sockets[1].fd = -1;
                    sockets[1].events = 0;
                }
            }
        }

    }

    return EXIT_SUCCESS;
}
