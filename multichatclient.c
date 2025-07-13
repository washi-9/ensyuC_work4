#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 10140
#define BUFFER_SIZE 1024
#define MAXCLIENTS 5

int sock;
volatile int running = 1;

void *receive_handler(void *arg) {
    char rbuf[BUFFER_SIZE];
    int bytesRcvd;
    while ((bytesRcvd = read(sock, rbuf, BUFFER_SIZE - 1)) > 0) {
        rbuf[bytesRcvd] = '\0';
        printf("%s", rbuf);
        fflush(stdout);
    }
    printf("\nConnection closed by server\n");
    running = 0;
    return NULL;
}

int main(int argc, char **argv) {
    struct sockaddr_in ServAddr;
    struct hostent *hp;
    pthread_t recv_thread;
    char rbuf[BUFFER_SIZE];
    char name[BUFFER_SIZE];
    int bytesRcvd;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <username>\n", argv[0]);
        exit(1);
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(1);
    }


    if ((hp = gethostbyname(argv[1])) == NULL) {
        perror("gethostbyname() failed");
        close(sock);
        exit(1);
    }

    memset(&ServAddr, 0, sizeof(ServAddr));
    ServAddr.sin_family = AF_INET;
    memcpy(&ServAddr.sin_addr, hp->h_addr, hp->h_length);
    ServAddr.sin_port = htons(PORT);

    if (connect(sock, (struct sockaddr *)&ServAddr, sizeof(ServAddr)) < 0) {
        perror("connect() failed");
        close(sock);
        exit(1);
    }

    bytesRcvd = read(sock, rbuf, BUFFER_SIZE);
    if (bytesRcvd > 0) {
        rbuf[bytesRcvd] = '\0';
    } else {
        perror("read() failed");
        close(sock);
        exit(1);
    }
    if (strncmp(rbuf, "REQUEST ACCEPTED\n", 17) != 0) {
        fprintf(stderr, "Server did not accept the connection.\n");
        close(sock);
        exit(1);
    } else {
        printf("join request accepted\n");
    }

    snprintf(name, sizeof(name), "%s\n", argv[2]);
    write(sock, name, strlen(name));
    bytesRcvd = read(sock, rbuf, BUFFER_SIZE);
    if (bytesRcvd > 0) {
        rbuf[bytesRcvd] = '\0';
    } else {
        perror("read() failed");
        close(sock);
        exit(1);
    }
    if (strncmp(rbuf, "USERNAME REGISTERED\n", 20) != 0) {
        fprintf(stderr, "user name rejected\n");
        close(sock);
        exit(1);
    } else {
        printf("user name registered\n");
    }

    if (pthread_create(&recv_thread, NULL, receive_handler, NULL) != 0) {
        perror("pthread_create failed");
        close(sock);
        exit(1);
    }

    while (running && fgets(rbuf, BUFFER_SIZE, stdin) != NULL) {
        if (write(sock, rbuf, strlen(rbuf)) < 0) {
            perror("write() failed");
            break;
        }
    }

    running = 0;
    close(sock);
    pthread_join(recv_thread, NULL);
    return 0;
}