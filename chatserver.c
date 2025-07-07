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
#include <signal.h>
#include <stdbool.h>
#include <errno.h>

#define PORT 10140
#define BUFFER_SIZE 1024
#define MAXCLIENTS 5
#define MAX_NAME_LENGTH 99

bool is_ctrl_c = false;

struct Client {
    int sock;
    char name[MAX_NAME_LENGTH + 1];
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
        if (client->is_named && strcmp(name, client->name) == 0) {
            return 0; // Name already exists
        }
    }
    return 1;
}

void handle_new_connection(int new_sock, int k, Client clients[]) {
    if (new_sock < 0) {
        perror("accept() failed");
        exit(1);
    }

    if (k+1 > MAXCLIENTS) {
        if (write(new_sock, "REQUEST REJECTED\n", 18) < 0) {
            perror("write failed");
        }
        printf("Connection rejected: too many clients\n");
        close(new_sock);
        return;
    } else {
        for (int i = 0; i < MAXCLIENTS; i++) {
            Client *client = &clients[i];
            if (client->sock == -1) {
                client->sock = new_sock;
                if (write(client->sock, "REQUEST ACCEPTED\n", 17) < 0) {
                    perror("write failed");
                }
                break;
            }
        }
    }
    return;
}

void ctrlC() {
    is_ctrl_c = true;
}

int main(int argc, char **argv) {
    int sock, new_sock, k = 0;
    fd_set rfds;
    struct timeval tv;
    struct sockaddr_in svr, clt;
    int clen, bytesRcvd, reuse;
    char rbuf[BUFFER_SIZE];
    Client clients[MAXCLIENTS];

    if(signal(SIGINT, ctrlC) == SIG_ERR) {
        perror("signal failed.");
        exit(1);
    }

    // Initialize client sockets and names
    for (int i = 0; i < MAXCLIENTS; i++) {
        clients[i].sock = -1;
        clients[i].is_named = 0;
        memset(clients[i].name, '\0', BUFFER_SIZE);
    }

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

    while (!is_ctrl_c) {
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        int maxfd = sock;

        for (int i = 0; i < MAXCLIENTS; i++) {
            Client *client = &clients[i];
            if (client->sock != -1) {
                FD_SET(client->sock, &rfds);
            }
            if (client->sock > maxfd) {
                maxfd = client->sock;
            }
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (activity < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select() failed");
            close(sock);
            exit(1);
        }

        if (activity > 0) {
            // new client connection
            if (FD_ISSET(sock, &rfds)) {
                clen = sizeof(clt);
                new_sock = accept(sock, (struct sockaddr *)&clt, &clen);
                handle_new_connection(new_sock, k, clients);
            }
    
            // message handling
            for (int i = 0; i < MAXCLIENTS; i++) {
                Client *client = &clients[i];

                if (client->sock > 0 && FD_ISSET(client->sock, &rfds)) {
                    bytesRcvd = read(client->sock, rbuf, BUFFER_SIZE);

                    if (client->is_named == 0) {
                        // register client name
                        if (bytesRcvd > 0) {
                            rbuf[bytesRcvd] = '\0';
                            rbuf[strcspn(rbuf, "\n")] = '\0';
                            // check name length
                            if (strlen(rbuf) > MAX_NAME_LENGTH) {
                                printf("Too long user name. The maximum length is %d. The overflowed part is not used\n", MAX_NAME_LENGTH);
                            }
                            char client_name[MAX_NAME_LENGTH + 1];
                            strncpy(client_name, rbuf, MAX_NAME_LENGTH + 1);
                            client_name[MAX_NAME_LENGTH] = '\0';
                            if (checkname(client_name, clients)) {
                                strcpy(client->name, client_name);
                                if (write(client->sock, "USERNAME REGISTERED\n", 20) < 0) {
                                    perror("write failed");
                                }
                                k++;
                            } else {
                                if (write(client->sock, "USERNAME REJECTED\n", 18) < 0) {
                                    perror("write failed");
                                }
                                close(client->sock);
                                client->sock = -1;
                                memset(client->name, '\0', MAX_NAME_LENGTH + 1);
                                continue; 
                            }
                            client->is_named = 1;
                            char message[BUFFER_SIZE + 16];
                            char* name = client->name;
                            name[strcspn(name, "\n")] = '\0';
                            snprintf(message, sizeof(message), "%s is registered.\n", name);
                            printf("%s", message);
                            continue;
                        } else {
                            close(client->sock);
                            client->sock = -1;
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
                        close(client->sock);
                        client->sock = -1;
                        client->is_named = 0;
                        memset(client->name, '\0', MAX_NAME_LENGTH + 1);
                        k--;
                    } else {
                        // Broadcast message to all clients
                        rbuf[bytesRcvd] = '\0';
                        for (int j = 0; j < MAXCLIENTS; j++) {
                            Client *all_client = &clients[j];
                            if (all_client->sock != -1) {
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

    close(sock);
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].sock > 0) { 
            close(clients[i].sock);
        }
    }
    printf("\nServer closed.\n");
    return 0;
}