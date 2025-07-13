#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/select.h>

#define PORT 10140
#define MAXCLIENTS 5
#define BUFFER_SIZE 1024
#define MAX_NAME_LENGTH 99

struct {
    int sock;
    char name[MAX_NAME_LENGTH + 1];
    pthread_t thread_id;
    int active;
} typedef Client;

Client clients[MAXCLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

bool is_ctrl_c = false;

void broadcast(const char *message) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].active) {
            if (write(clients[i].sock, message, BUFFER_SIZE) < 0) {
                perror("write failed");
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void *arg) {
    int sock = *(int *)arg;
    int bytesRcvd;
    char name[MAX_NAME_LENGTH + 1];
    char rbuf[BUFFER_SIZE];

    
    pthread_mutex_lock(&clients_mutex);
    int index = -1;
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (!clients[i].active) {
            index = i;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    
    if (index == -1) {
        if (write(sock, "REQUEST REJECTED\n", 18) < 0) {
            perror("write failed");
            close(sock);
            return NULL;
        }
        close(sock);
        return NULL;
    }
    
    if (write(sock, "REQUEST ACCEPTED\n", 18) < 0) {
        perror("write failed");
        close(sock);
        return NULL;
    }

    // read user name
    if (read(sock, rbuf, sizeof(rbuf)) <= 0) {
        close(sock);
        return NULL;
    }

    rbuf[sizeof(rbuf) - 1] = '\0';
    rbuf[strcspn(rbuf, "\n")] = '\0';

    if (strlen(rbuf) > MAX_NAME_LENGTH) {
        printf("Too long user name. The maximum length is %d. The overflowed part is not used\n", MAX_NAME_LENGTH);
    }
    strncpy(name, rbuf, MAX_NAME_LENGTH+1);
    name[MAX_NAME_LENGTH] = '\0';

    int valid = 1;
    for (int i = 0; i < strlen(name); i++) {
        if (isalnum(name[i]) == 0 && name[i] != '-' && name[i] != '_') {
            valid = 0;
            break;
        }
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].active && strcmp(clients[i].name, name) == 0) {
            valid = 0;
            break;
        }
    }

    if (!valid) { // valid name
        write(sock, "USERNAME REJECTED\n", 18);
        pthread_mutex_unlock(&clients_mutex);
        close(sock);
        return NULL;
    }

    strncpy(clients[index].name, name, MAX_NAME_LENGTH);
    clients[index].name[MAX_NAME_LENGTH] = '\0';
    clients[index].sock = sock;
    clients[index].active = 1;

    printf("%s is registered.\n", clients[index].name);
    if (write(sock, "USERNAME REGISTERED\n",20) < 0) {
        perror("write failed");
        close(sock);
        clients[index].active = 0;
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&clients_mutex);

    char message[BUFFER_SIZE*2];
    snprintf(message, sizeof(message), "%s joined the chat\n", clients[index].name);
    broadcast(message);  

    while (!is_ctrl_c) {
        bytesRcvd = read(sock, rbuf, BUFFER_SIZE - 1);
        if (bytesRcvd <= 0) {
            printf("%s left the chat\n", clients[index].name);
            break;
        }
        rbuf[bytesRcvd] = '\0';

        snprintf(message, BUFFER_SIZE*2, "%s >%s", clients[index].name, rbuf);
        broadcast(message);
    }

    close(sock);

    pthread_mutex_lock(&clients_mutex);
    clients[index].active = 0;
    memset(clients[index].name, '\0', MAX_NAME_LENGTH + 1);
    clients[index].sock = -1;
    pthread_mutex_unlock(&clients_mutex);

    snprintf(message, sizeof(message), "%s left the chat\n", clients[index].name);
    broadcast(message);

    return NULL;
}

void ctrlC() {
    is_ctrl_c = true;
}

int main() {
    int sock, reuse;
    fd_set rfds;
    struct sockaddr_in svr, clt;
    struct timeval tv;
    socklen_t addr_len = sizeof(clt);

    if(signal(SIGINT, ctrlC) == SIG_ERR) {
        perror("signal failed.");
        exit(1);
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(1); 
    }

    reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt() failed");
        close(sock);
        exit(1);
    }
    
    svr.sin_family = AF_INET;
    svr.sin_addr.s_addr = htonl(INADDR_ANY);
    svr.sin_port = htons(PORT);

    if (bind(sock, (struct sockaddr *)&svr, sizeof(svr)) < 0) {
        perror("bind failed");
        close(sock);
        exit(1);
    }

    if (listen(sock, MAXCLIENTS) < 0) {
        perror("listen failed");
        close(sock);
        exit(1);
    }

    while (!is_ctrl_c) {
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);

        int maxfd = sock;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int activity = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select failed");
            break;
        }

        if (is_ctrl_c) {
            break;
        }
        if (FD_ISSET(sock, &rfds)) {
            int csock = accept(sock, (struct sockaddr *)&clt, &addr_len);
            if (csock < 0) {
                if (errno == EINTR) break;
                perror("accept failed");
                continue;
            }
            
            pthread_t tid;
            int *pclient = malloc(sizeof(int));
            *pclient = csock;
            pthread_create(&tid, NULL, handle_client, pclient);
            pthread_detach(tid);
        }
    }

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].active) {
            close(clients[i].sock);
            clients[i].active = 0;
            memset(clients[i].name, '\0', MAX_NAME_LENGTH + 1);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
    pthread_mutex_destroy(&clients_mutex);
    printf("\nserver shutdown\n");

    close(sock);
    return 0;
}