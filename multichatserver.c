#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 10140
#define MAXCLIENTS 5
#define BUFFER_SIZE 1024
#define MAX_NAME_LENGTH 99

typedef struct {
    int sock;
    char name[MAX_NAME_LENGTH + 1];
    pthread_t thread_id;
    int active;
} Client;

Client clients[MAXCLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    char name[BUFFER_SIZE];
    char buffer[BUFFER_SIZE];

    
    pthread_mutex_lock(&clients_mutex);
    int index = -1;
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (!clients[i].active) {
            // clients[i].sock = sock;
            // strncpy(clients[i].name, name, MAX_NAME_LENGTH);
            // clients[i].active = 1;
            index = i;
            break;
        }
    }
    
    if (index != -1) {
        if (write(sock, "REQUEST ACCEPTED\n", 18) < 0) {
            perror("write failed");
            close(sock);
            pthread_mutex_unlock(&clients_mutex);
            return NULL;
        }
    } else {
        if (write(sock, "REQUEST REJECTED\n", 18) < 0) {
            perror("write failed");
            close(sock);
            pthread_mutex_unlock(&clients_mutex);
            return NULL;
        }
    }
    
    if (read(sock, name, sizeof(name)) <= 0) {
        close(sock);
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }


    if (strlen(name) > MAX_NAME_LENGTH) {
        printf("Too long user name. The maximum length is %d. The overflowed part is not used\n", MAX_NAME_LENGTH);
    }

    name[strcspn(name, "\n")] = '\0';
    int nameFlag = 1;
    for (int i = 0; i < strlen(name); i++) {
        if (isalnum(name[i]) == 0 && name[i] != '-' && name[i] != '_') {
            nameFlag = 0;
            break;
        }
    }
    for (int i = 0; i < MAXCLIENTS; i++) {
        if (clients[i].active && strcmp(name, clients[i].name) == 0) {
            nameFlag = 0;
            break;
        }
    }
    if (nameFlag) { // valid name
        if (write(sock, "USERNAME REGISTERED\n", 20) < 0) {
            perror("write failed");
            close(sock);
            return NULL;
        }
        memset(name, '\0', MAX_NAME_LENGTH + 1);
        strcpy(clients[index].name, name);
        printf("%s is registered.\n", clients[index].name);
        clients[index].sock = sock;
        clients[index].active = 1;
    } else {
        if (write(sock, "USERNAME REJECTED\n", 18) < 0) {
            perror("write failed");
        }
        close(sock);
        pthread_mutex_unlock(&clients_mutex);
        return NULL;
    }
    pthread_mutex_unlock(&clients_mutex);

    char message[BUFFER_SIZE*2];
    snprintf(message, sizeof(message), "%s joined the chat\n", clients[index].name);
    broadcast(message);  

    while(1) {
        int RbytesRcvd = read(sock, buffer, BUFFER_SIZE - 1);
        if (RbytesRcvd <= 0) {
            break;
        }
        buffer[RbytesRcvd] = '\0';

        snprintf(message, BUFFER_SIZE, "%s >%s", clients[index].name, buffer);
        broadcast(message);
    }

    close(sock);

    pthread_mutex_lock(&clients_mutex);
    clients[index].active = 0;
    pthread_mutex_unlock(&clients_mutex);

    // char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "%s left the chat\n", clients[index].name);
    broadcast(message);

    return NULL;
}

int main() {
    int sock, reuse;
    struct sockaddr_in svr, clt;
    socklen_t addr_len = sizeof(clt);

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

    while (1) {
        int csock = accept(sock, (struct sockaddr *)&clt, &addr_len);
        if (csock < 0) {
            perror("accept failed");
            continue;
        }

        pthread_t tid;
        int *pclient = malloc(sizeof(int));
        *pclient = csock;
        pthread_create(&tid, NULL, handle_client, pclient);
        pthread_detach(tid);
    }

    close(sock);
    return 0;
}