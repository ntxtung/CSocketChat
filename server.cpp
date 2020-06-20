#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <iostream>
#include <string> 

#include <pthread.h>
#include <semaphore.h>
using namespace std;

#define PORT "3490" // the port users will be connecting to
#define MAXDATASIZE 100
#define BACKLOG 100 // how many pending connections queue will hold

struct ChatClient {
    int socketId;
    char name[20];
};

ChatClient clientSocketList[BACKLOG];
int numCurrentClient = 0;

char* getClientNameById(int clientSocketId) {
    for (int i=0; i<numCurrentClient; i++){
        if (clientSocketList[i].socketId == clientSocketId)
            return clientSocketList[i].name;
    }
}

void addClient(int clientSocketId, char* clientName) {
    numCurrentClient+=1;
    clientSocketList[numCurrentClient-1].socketId = clientSocketId;

    string realName = "Client " + to_string(clientSocketId);
    // strcpy(clientSocketList[numCurrentClient-1].name, realName.c_str());
    strcpy(clientSocketList[numCurrentClient-1].name, clientName);
}

void broadcastToAll(char* message, int fromId) {
    char realMessage[MAXDATASIZE];
    sprintf(realMessage, "%s: %s", getClientNameById(fromId), message);
    printf("%s\n",realMessage);
    for (int i=0; i<numCurrentClient; i++){
        send(clientSocketList[i].socketId, realMessage, strlen(realMessage), 0);
    }
}

void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;

    errno = saved_errno;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET)
    {
        return &(((struct sockaddr_in *)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void *clientThread(void *arg) {
    int clientSocket = *((int*)(&arg));
    int numbytes;
    char buf[MAXDATASIZE];
    // Get name
    // numbytes = recv(clientSocket, buf, MAXDATASIZE - 1, 0);
    // buf[numbytes] = '\0';
    while (1)
    {
        if ((numbytes = recv(clientSocket, buf, MAXDATASIZE - 1, 0)) == -1)
        {
            perror("recv");
            exit(1);
        }

        buf[numbytes] = '\0';
        if (strlen(buf) > 0)
        {
            // printf("%d: %s\n", clientSocket, buf);
            broadcastToAll(buf, clientSocket);
        }
    }
}

int main(void)
{
    int compSocket, clientSocket; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage their_addr; // connector's address information
    socklen_t sin_size;
    struct sigaction sa;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((compSocket = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            perror("server: socket");
            continue;
        }

        if (setsockopt(compSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
        {
            perror("setsockopt");
            exit(1);
        }

        if (bind(compSocket, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(compSocket);
            perror("server: bind");
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)
    {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(compSocket, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");
    while (1)
    {
        sin_size = sizeof their_addr;
        clientSocket = accept(compSocket, (struct sockaddr *)&their_addr, &sin_size);
        if (clientSocket == -1)
        {
            perror("accept");
            continue;
        }

        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("Server: got connection from %s with id %d\n", s, clientSocket);
        // Get Name
        char buf[20];
        int numbytes = recv(clientSocket, buf, 20 - 1, 0);
        buf[numbytes] = '\0';
        addClient(clientSocket, buf);

        pthread_t clientThread_t;
        pthread_create(&clientThread_t, NULL, clientThread, (void*)clientSocket);
    }

    return 0;
}