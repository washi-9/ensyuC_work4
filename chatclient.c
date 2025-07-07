#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<netdb.h>
#include<unistd.h>
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
#include<ctype.h>
#include<sys/select.h>

#define PORT 10140
#define BUFFER_SIZE 1024
#define MAXCLIENTS 5

int state = 1;

int main(int argc, char **argv) {
    int sock;
    struct sockaddr_in ServAddr;
    struct hostent *hp;
    char *servIP;
    char rbuf[BUFFER_SIZE];
    char name[BUFFER_SIZE];
    int bytesRcvd;
    struct timeval tv;
    fd_set rfds;
    setbuf(stdout, NULL);

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <username>\n", argv[0]);
        exit(1);
    }

    
    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(1);
    }
    
    servIP = argv[1];
    
    if ((hp = gethostbyname(servIP)) == NULL) {
        perror("gethostbyname() failed");
        exit(1);
    }

    memset(&ServAddr, 0, sizeof(ServAddr));
    ServAddr.sin_family = AF_INET;
    memcpy(&ServAddr.sin_addr, hp->h_addr, hp->h_length);
    ServAddr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0) {
        perror("connect() failed");
        close(sock);
        exit(1);
    }

    // state 2 start
    state = 2;

    bytesRcvd = read(sock, rbuf, BUFFER_SIZE);
    if (bytesRcvd > 0) {
        rbuf[bytesRcvd] = '\0';
    } else {
        state = 6;
    }
    if (strncmp(rbuf, "REQUEST ACCEPTED\n", 17) != 0) {
        fprintf(stderr, "Server did not accept the connection.\n");
        state = 6;
        exit(1);
    } else {
        printf("join request accepted\n");
    }

    // state 3 start
    state = 3;

    snprintf(name, sizeof(name), "%s", argv[2]); // argv[2] + \n
    write(sock, name, strlen(name));
    bytesRcvd = read(sock, rbuf, BUFFER_SIZE);
    if (bytesRcvd > 0) {
        rbuf[bytesRcvd] = '\0';
    } else {
        state = 6;
    }
    if (strncmp(rbuf, "USERNAME REGISTERED\n", 20) != 0) {
        fprintf(stderr, "user name rejected\n");
        state = 6;
        exit(1);
    } else {
        printf("user name registered\n");
    }

    // state 4 start
    state = 4;

    while(state == 4) {
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
        FD_SET(sock, &rfds);

        int maxfd = sock > 0 ? sock : 0;

        tv.tv_sec = 1;
        tv.tv_usec = 0;

        if (select(maxfd + 1, &rfds, NULL, NULL, &tv) > 0) {
            if (FD_ISSET(0, &rfds)) {
                // Read from stdin
                if (fgets(rbuf, BUFFER_SIZE, stdin) != NULL) {
                    size_t len = strlen(rbuf);
                    if (rbuf[0] == EOF) {
                        state = 5;
                        break;
                    }
                    if (write(sock, rbuf, len) != len) {
                        perror("write() failed");
                        break;
                    }
                } else {
                    if (feof(stdin)) {
                        state = 5;
                        break;
                    } else {
                        if (feof(stdin)) {
                            state = 5;
                            break;
                        } else {
                        perror("fgets() failed");
                        state = 6;
                        break;
                        }
                    }
                }
            }

            if (FD_ISSET(sock, &rfds)) {
                // Read from socket
                bytesRcvd = read(sock, rbuf, BUFFER_SIZE - 1);
                if (bytesRcvd <= 0) {
                    perror("read() failed or connection closed");
                    state = 6;
                    break;
                }
                rbuf[bytesRcvd] = '\0';
                printf("%s", rbuf);
            }
        }
    }

    // state 5 start
    if (state == 5) {
        close(sock);
        printf("Exit chatclient\n");
        exit(0);
    } else if (state == 6) {
        close(sock);
        printf("Error occurred\n");
        exit(1);
    }
}