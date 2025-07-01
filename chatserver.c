#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/select.h>

#define PORT 10140
#define BUFFER_SIZE 1024
#define MAXCLIENTS 5

int state = 1;

struct Client {
    int sock;
    char name[BUFFER_SIZE];
    int is_named;
} typedef Client;

int checkname(const char *name, Client clients[]) {
    for (int i = 0; i < strlen(name); i++) {
        if (isalnum(name[i]) == 0 && name[i] != '-' && name[i] != '_') {
            return 0; // Invalid character in name
        }
    }
    for (int i = 0; i < MAXCLIENTS; i++) {
        Client *client = &clients[i];
        // if (cname[i][0] != '\0' && strcmp(name, cname[i]) == 0) {
        if (client->is_named && strcmp(name, client->name) == 0) {
            return 0; // Name already exists
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    int sock, new_sock, k = 0;
    fd_set rfds;
    struct timeval tv;
    struct sockaddr_in svr, clt;
    int clen, bytesRcvd, reuse;
    char rbuf[BUFFER_SIZE];
    Client clients[MAXCLIENTS];

    // Initialize client sockets and names
    for (int i = 0; i < MAXCLIENTS; i++) {
        clients[i].sock = 0;
        clients[i].is_named = 0;
        memset(clients[i].name, '\0', BUFFER_SIZE);
    }

    // state 1 start
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(1);
    }

    reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt() failed");
        close(sock);
        exit(1);
    }

    bzero(&svr, sizeof(svr));
    svr.sin_family = AF_INET;
    svr.sin_addr.s_addr = htonl(INADDR_ANY);
    svr.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr *)&svr, sizeof(svr)) < 0) {
        perror("bind() failed");
        close(sock);
        exit(1);
    }

    if (listen(sock, MAXCLIENTS) < 0) {
        perror("listen() failed");
        close(sock);
        exit(1);
    }

    // state 2 start
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        FD_SET(sock, &rfds);

        int maxfd = sock;

        for (int i = 0; i < MAXCLIENTS; i++) {
            Client *client = &clients[i];
            if (client->sock > 0) {
                FD_SET(client->sock, &rfds);
            }
            if (client->sock > maxfd) {
                maxfd = client->sock;
            }
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        // state 3 start
        int activity = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (activity < 0) {
            perror("select() failed");
            close(sock);
            exit(1);
        }

        if (activity > 0) {
            // new client connection
            // start state 4
            if (FD_ISSET(sock, &rfds)) {
                clen = sizeof(clt);
                new_sock = accept(sock, (struct sockaddr *)&clt, &clen);
                if (new_sock < 0) {
                    perror("accept() failed");
                    close(sock);
                    exit(1);
                }
                int cnum;
    
                if (k+1 > MAXCLIENTS) {
                    write(new_sock, "REQUEST REJECTED\n", 18);
                    printf("Connection rejected: too many clients\n");
                    close(new_sock);
                } else {
                    for (int i = 0; i < MAXCLIENTS; i++) {
                        Client *client = &clients[i];
                        if (client->sock == 0) {
                            cnum = i;
                            client->sock = new_sock;
                            write(new_sock, "REQUEST ACCEPTED\n", 17);
                            break;
                        }
                    }
                }
            }
    
            // message handling
            for (int i = 0; i < MAXCLIENTS; i++) {
                Client *client = &clients[i];
                int sd = client->sock;
                int sdi = i;
    
                if (FD_ISSET(sd, &rfds)) {
                    bytesRcvd = read(sd, rbuf, BUFFER_SIZE);
                    if (client->is_named == 0) {
                        // register client name
                        if (bytesRcvd > 0) {
                            rbuf[bytesRcvd] = '\0';
                            rbuf[strcspn(rbuf, "\n")] = '\0';
                            if (checkname(rbuf, clients)) {
                                strncpy(client->name, rbuf, BUFFER_SIZE - 1);
                                client->is_named = 1;
                                write(sd, "USERNAME REGISTERED\n", 20);
                                k++;
                            } else {
                                write(sd, "USERNAME REJECTED\n", 18);
                                close(sd);
                                client->sock = 0;
                                memset(client->name, '\0', BUFFER_SIZE);
                                continue; // Skip further processing for this client
                            }
                            client->is_named = 1;
                            char message[BUFFER_SIZE + 16];
                            char* name = client->name;
                            name[strcspn(name, "\n")] = '\0';
                            snprintf(message, sizeof(message), "%s is registered.\n", name);
                            printf("%s", message);
                            break;
                        } else {
                            close(sd);
                            client->sock = 0;
                        }
                        continue;
                    }
    
                    if (bytesRcvd <= 0) {
                        // Client disconnected
                        for (int j = 0; j < MAXCLIENTS; j++) {
                            Client *other_client = &clients[j];
                            if (other_client->sock > 0 && other_client != client) {
                                char message[BUFFER_SIZE + 16];
                                char* name = client->name;
                                name[strcspn(name, "\n")] = '\0';
                                snprintf(message, sizeof(message), "%s left the chat.\n", name);
                                if (write(other_client->sock, message, strlen(message)) < 0) {
                                    perror("write failed");
                                }
                            }
                        }
                        char message[BUFFER_SIZE + 16];
                        char* name = client->name;
                        name[strcspn(name, "\n")] = '\0';
                        snprintf(message, sizeof(message), "%s left the chat.\n", name);
                        printf("%s", message);
                        close(sd);
                        client->sock = 0;
                        client->is_named = 0;
                        memset(client->name, '\0', BUFFER_SIZE);
                        k--;
                    } else {
                        // Broadcast message to all clients
                        rbuf[bytesRcvd] = '\0'; // Null-terminate the string
                        for (int j = 0; j < MAXCLIENTS; j++) {
                            Client *all_client = &clients[j];
                            if (all_client->sock != 0) {
                                char message[BUFFER_SIZE * 2];
                                char* name = client->name;
                                name[strcspn(name, "\n")] = '\0';
                                snprintf(message, sizeof(message), "%s >%s", name, rbuf);
                                if (write(all_client->sock, message, strlen(message)) < 0) {
                                    perror("write failed");
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}