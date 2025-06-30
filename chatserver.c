#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 10140
#define BUFFER_SIZE 1024
#define MAXCLIENTS 5

int state = 1;

int checkname(const char *name) {
    return 1;
}

int main(int argc, char **argv) {
    int sock, new_sock, k = 0;
    int csock[MAXCLIENTS];
    char cname[MAXCLIENTS][BUFFER_SIZE];
    int cnamecheck[MAXCLIENTS] = {0};
    fd_set rfds;
    struct timeval tv;
    struct sockaddr_in svr, clt;
    int clen, bytesRcvd, reuse;
    char rbuf[BUFFER_SIZE];

    // Initialize client sockets and names
    for (int i = 0; i < MAXCLIENTS; i++) {
        csock[i] = 0;
        memset(cname[i], '\0', BUFFER_SIZE);
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
            if (csock[i] > 0) {
                FD_SET(csock[i], &rfds);
            }
            if (csock[i] > maxfd) {
                maxfd = csock[i];
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
                // state 4 start
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
                        if (csock[i] == 0) {
                            cnum = i;
                            csock[cnum] = new_sock;
                            write(new_sock, "REQUEST ACCEPTED\n",17);
                            // printf("New client connected: %d\n", cnum);
                            break;
                        }
                    }
                }
            }
    
            // message handling
            for (int i = 0; i < MAXCLIENTS; i++) {
                int sd = csock[i];
                int sdi = i;
    
                if (FD_ISSET(sd, &rfds)) {
                    bytesRcvd = read(sd, rbuf, BUFFER_SIZE);
                    if (cnamecheck[sdi] == 0) {
                        // register client name
                        if (bytesRcvd > 0) {
                            rbuf[bytesRcvd] = '\0';
                            strncpy(cname[sdi], rbuf, BUFFER_SIZE - 1); // name + '\0'
                            if (checkname(cname[sdi])) {
                                cnamecheck[sdi] = 1;
                                write(sd, "USERNAME REGISTERED\n", 20);
                                k++;
                            } else {
                                write(sd, "USERNAME REJECTED\n", 18);
                                close(sd);
                                csock[sdi] = 0;
                                memset(cname[sdi], '\0', BUFFER_SIZE);
                                continue; // Skip further processing for this client
                            }
                            cnamecheck[sdi] = 1; // Mark name as registered
                            // write(sd, "NAME REGISTERED\n", 17);
                            char message[BUFFER_SIZE + 16];
                            char* name = cname[sdi];
                            name[strcspn(name, "\n")] = 0;
                            snprintf(message, sizeof(message), "%s is registered.\n", name);
                            printf("%s", message);
                            break;
                        } else {
                            close(sd);
                            csock[sdi] = 0;
                        }
                        continue;
                    }
    
                    if (bytesRcvd <= 0) {
                        // Client disconnected
                        for (int j = 0; j < MAXCLIENTS; j++) {
                            if (csock[j] > 0 && j != sdi) {
                                char message[BUFFER_SIZE + 16];
                                // write(csock[j], cname[sdi], strlen(cname[sdi]));
                                // write(csock[j], " has left the chat.\n", 20);
                                char* name = cname[sdi];
                                name[strcspn(name, "\n")] = 0;
                                snprintf(message, sizeof(message), "%s left the chat.\n", name);
                                if (write(csock[j], message, strlen(message)) < 0) {
                                    perror("write failed");
                                }
                            }
                        }
                        char message[BUFFER_SIZE + 16];
                        char* name = cname[sdi];
                        name[strcspn(name, "\n")] = 0;
                        snprintf(message, sizeof(message), "%s left the chat.\n", name);
                        printf("%s", message);
                        close(sd);
                        csock[i] = 0;
                        cnamecheck[sdi] = 0;
                        memset(cname[i], '\0', BUFFER_SIZE);
                        k--;
                    } else {
                        // Broadcast message to all clients
                        rbuf[bytesRcvd] = '\0'; // Null-terminate the string
                        for (int j = 0; j < MAXCLIENTS; j++) {
                            if (csock[j] != 0) {
                                char message[BUFFER_SIZE * 2];
                                char* name = cname[sdi];
                                name[strcspn(name, "\n")] = 0;
                                snprintf(message, sizeof(message), "%s >%s", name, rbuf);
                                if (write(csock[j], message, strlen(message)) < 0) {
                                    perror("write failed");
                                }
                                
                                // write(csock[j], cname[sdi], strlen(cname[sdi]));
                                // write(csock[j], ": ", 2);
                                // write(csock[j], rbuf, bytesRcvd);
                            }
                        }
                        // for (int j = 0; j < MAXCLIENTS; j++) {
                        //     if (csock[j] > 0 && j != sdi) {
                        //         write(csock[j], cname[sdi], strlen(cname[sdi]));
                        //         write(csock[j], ": ", 2);
                        //         write(csock[j], rbuf, bytesRcvd);
                        //     }
                        // }
                    }
                }
            }
        }
    }
}