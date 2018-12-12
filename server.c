//Eugene Li - Multithreaded chat server
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>

#define MAX_BUFFER 1024

/*
Queue implementation using a char array.
Contains a mutex for functions to lock on before modifying the array,
and condition variables for when it's not empty or full.
*/
typedef struct {
    char *buffer[MAX_BUFFER];
    int head, tail;
    int full, empty;
    pthread_mutex_t *mutex;
    pthread_cond_t *notFull, *notEmpty;
} queue;

/*
Struct containing important data for the server to work.
Namely the list of client sockets, that list's mutex,
the server's socket for new connections, and the message queue
*/
typedef struct {
    fd_set serverReadFds;
    int socketFd;
    int clientSockets[MAX_BUFFER];
    int numClients;
    char name[MAX_BUFFER][100];
    pthread_mutex_t *clientListMutex;
    queue *queue;
} chatDataVars;

/*
Simple struct to hold the chatDataVars and the new client's socket fd.
Used only in the client handler thread.
*/
typedef struct {
    chatDataVars *data;
    int clientSocketFd;
} clientHandlerVars;

void startChat(int socketFd);
void buildMessage(char *result, char *name, char *msg);
void bindSocket(struct sockaddr_in *serverAddr, int socketFd, long port);
void removeClient(chatDataVars *data, int clientSocketFd);

void *newClientHandler(void *data);
void *clientHandler(void *chv);
void *messageHandler(void *data);

void queueDestroy(queue *q);
queue* queueInit(void);
void queuePush(queue *q, char* msg);
char* queuePop(queue *q);

void listClient(chatDataVars *data,char *online,int clientSocketFd);
void send_to(chatDataVars *data,char *name,char *msg,int fd);
void file_to(chatDataVars *data,char *name,char *msg,int fd,char *file);

int main(int argc, char *argv[])
{
    struct sockaddr_in serverAddr;
    long port = 8888;
    int socketFd;

//    if(argc == 2) port = strtol(argv[1], NULL, 0);

    if((socketFd = socket(AF_INET, SOCK_STREAM, 0))== -1)
    {
        perror("Socket creation failed");
        exit(1);
    }

    bindSocket(&serverAddr, socketFd, port);
    if(listen(socketFd, 1) == -1)
    {
        perror("listen failed: ");
        exit(1);
    }

    startChat(socketFd);
    
    close(socketFd);
}

//Spawns the new client handler thread and message consumer thread
void startChat(int socketFd)
{
    chatDataVars data;
    data.numClients = 0;
    data.socketFd = socketFd;
    data.queue = queueInit();
    data.clientListMutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(data.clientListMutex, NULL);

    //Start thread to handle new client connections
    pthread_t connectionThread;
    if((pthread_create(&connectionThread, NULL, (void *)&newClientHandler, (void *)&data)) == 0)
    {
        fprintf(stderr, "Connection handler started\n");
    }

    FD_ZERO(&(data.serverReadFds));
    FD_SET(socketFd, &(data.serverReadFds));

    //Start thread to handle messages received
    pthread_t messagesThread;
    if((pthread_create(&messagesThread, NULL, (void *)&messageHandler, (void *)&data)) == 0)
    {
        fprintf(stderr, "Message handler started\n");
    }

    pthread_join(connectionThread, NULL);
    pthread_join(messagesThread, NULL);

    queueDestroy(data.queue);
    pthread_mutex_destroy(data.clientListMutex);
    free(data.clientListMutex);
}

//Initializes queue
queue* queueInit(void)
{
    queue *q = (queue *)malloc(sizeof(queue));
    if(q == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }

    q->empty = 1;
    q->full = q->head = q->tail = 0;
    q->mutex = (pthread_mutex_t *) malloc(sizeof(pthread_mutex_t));
    if(q->mutex == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(q->mutex, NULL);

    q->notFull = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    if(q->notFull == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);   
    }
    pthread_cond_init(q->notFull, NULL);

    q->notEmpty = (pthread_cond_t *) malloc(sizeof(pthread_cond_t));
    if(q->notEmpty == NULL)
    {
        perror("Couldn't allocate anymore memory!");
        exit(EXIT_FAILURE);
    }
    pthread_cond_init(q->notEmpty, NULL);

    return q;
}

//Frees a queue
void queueDestroy(queue *q)
{
    pthread_mutex_destroy(q->mutex);
    pthread_cond_destroy(q->notFull);
    pthread_cond_destroy(q->notEmpty);
    free(q->mutex);
    free(q->notFull);
    free(q->notEmpty);
    free(q);
}

//Push to end of queue
void queuePush(queue *q, char* msg)
{
    q->buffer[q->tail] = msg;
    q->tail++;
    if(q->tail == MAX_BUFFER)
        q->tail = 0;
    if(q->tail == q->head)
        q->full = 1;
    q->empty = 0;
}

//Pop front of queue
char* queuePop(queue *q)
{
    char* msg = q->buffer[q->head];
    q->head++;
    if(q->head == MAX_BUFFER)
        q->head = 0;
    if(q->head == q->tail)
        q->empty = 1;
    q->full = 0;

    return msg;
}

//Sets up and binds the socket
void bindSocket(struct sockaddr_in *serverAddr, int socketFd, long port)
{
    memset(serverAddr, 0, sizeof(*serverAddr));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr->sin_port = htons(port);

    if(bind(socketFd, (struct sockaddr *)serverAddr, sizeof(struct sockaddr_in)) == -1)
    {
        perror("Socket bind failed: ");
        exit(1);
    }
}

//Removes the socket from the list of active client sockets and closes it
void removeClient(chatDataVars *data, int clientSocketFd)
{
    pthread_mutex_lock(data->clientListMutex);
    for(int i = 0; i < MAX_BUFFER; i++)
    {
        if(data->clientSockets[i] == clientSocketFd)
        {
            data->clientSockets[i] = 0;
            close(clientSocketFd);
            data->numClients--;
            i = MAX_BUFFER;
        }
    }
    pthread_mutex_unlock(data->clientListMutex);
}
void listClient(chatDataVars *data,char *online,int clientSocketFd)
{
	int a;
	char num[25];
	for(int i=0;i<MAX_BUFFER;i++){
		if(data->clientSockets[i]>0){
			a=data->clientSockets[i];
			strcat(online,data->name[a]);
			strcat(online,"\n");
		}
	}
	sprintf(num,"%d",data->numClients);
	strcat(num," users are online\n");
	strcat(online,num);
	if(write(clientSocketFd,online,MAX_BUFFER-1)==-1)
		perror("list failed.");
}	
void send_to(chatDataVars *data,char *name,char *msg,int fd)
{
	int a,b;
	char private[MAX_BUFFER],*ptr;
	char sender[MAX_BUFFER]="Private message from ";
	char cp_name[100];
	strcpy(private,msg);
	strcpy(cp_name,data->name[fd]);
	ptr=strtok(private," ");
	ptr=strtok(NULL," ");
	ptr=strtok(NULL,"\n");
	strcat(ptr,"\n");
	strcat(cp_name," : ");
	strcat(sender,cp_name);
	strcat(sender,ptr);
	for(int i=0;i<data->numClients;i++){
		a=data->clientSockets[i];
		if(strcmp(name,data->name[a])==0){
			write(a,sender,MAX_BUFFER-1);
			break;
		}
		if(i==data->numClients-1)
			write(fd,"Can't find the user.\n",MAX_BUFFER-1);
	}
}
void file_to(chatDataVars *data,char *name,char *msg,int fd,char *file)
{
	FILE *fp;
	int a,recv_fd;
	char buf[MAX_BUFFER];
	char cp_msg[MAX_BUFFER],*ptr,*qtr;
	char hint[MAX_BUFFER]="A file \"";
	char sender_name[100],file_name[100];
	strcpy(cp_msg,msg);
	ptr=strtok(cp_msg," ");
	ptr=strtok(NULL," ");
	ptr=strtok(NULL,"\n");	//ptr = file name
	strcpy(file_name,ptr);
	strcat(file_name,"\n");

	strcat(hint,ptr);
	strcat(hint,"\" from ");
	strcpy(sender_name,data->name[fd]);
	strcat(hint,sender_name);
	strcat(hint,"! Accept or not(y/n)?\n");
	for(int i=0;i<data->numClients;i++){
		a=data->clientSockets[i];
		if(strcmp(name,data->name[a])==0){
			recv_fd=a;
			write(a,hint,MAX_BUFFER-1);
			break;
		}
		if(i==data->numClients-1)
			write(fd,"Can't find the user.\n",MAX_BUFFER-1);
	}
	memset(&buf,0,sizeof(buf));
	a=read(recv_fd,buf,MAX_BUFFER-1);
	buf[a]='\0';
	qtr=strtok(buf," ");
	qtr=strtok(NULL,"\n");
	if(strcmp(qtr,"Y")==0 || strcmp(qtr,"yes")==0 ||strcmp(qtr,"y")==0){
		write(fd,"The user accpeted your request.\n",MAX_BUFFER-1);
		write(recv_fd,file,MAX_BUFFER-1);
		write(recv_fd,file_name,MAX_BUFFER-1);
	}
	else
		write(fd,"The user denied your request.\n",MAX_BUFFER-1);
	

}
//Thread to handle new connections. Adds client's fd to list of client fds and spawns a new clientHandler thread for it
void *newClientHandler(void *data)
{
    int a;
    char arr[100];
    chatDataVars *chatData = (chatDataVars *) data;
    while(1)
    {
        int clientSocketFd = accept(chatData->socketFd, NULL, NULL);
        if(clientSocketFd > 0)
        {
            fprintf(stderr, "Server accepted new client. Socket: %d\n", clientSocketFd);

            //Obtain lock on clients list and add new client in
            pthread_mutex_lock(chatData->clientListMutex);
            if(chatData->numClients < MAX_BUFFER)
            {
                //Add new client to list
                for(int i = 0; i < MAX_BUFFER; i++)
                {
                    if(!FD_ISSET(chatData->clientSockets[i], &(chatData->serverReadFds)))
                    {
                        chatData->clientSockets[i] = clientSocketFd;
			a=read(clientSocketFd,arr,99);
			arr[a]='\0';
			strcpy(chatData->name[clientSocketFd],arr);
                        i = MAX_BUFFER;
                    }
                }

                FD_SET(clientSocketFd, &(chatData->serverReadFds));

                //Spawn new thread to handle client's messages
                clientHandlerVars chv;
                chv.clientSocketFd = clientSocketFd;
                chv.data = chatData;

                pthread_t clientThread;
                if((pthread_create(&clientThread, NULL, (void *)&clientHandler, (void *)&chv)) == 0)
                {
                    chatData->numClients++;
                    fprintf(stderr, "Client has joined chat. Username: %s\n", chatData->name[clientSocketFd]);
                }
                else
                    close(clientSocketFd);
            }
            pthread_mutex_unlock(chatData->clientListMutex);
        }
    }
}

//The "producer" -- Listens for messages from client to add to message queue
void *clientHandler(void *chv)
{
    clientHandlerVars *vars = (clientHandlerVars *)chv;
    chatDataVars *data = (chatDataVars *)vars->data;

    queue *q = data->queue;
    int clientSocketFd = vars->clientSocketFd;

    char f[MAX_BUFFER]="#FILE";
    char msgBuffer[MAX_BUFFER],copy[MAX_BUFFER];
    char online[MAX_BUFFER],*ptr,file[MAX_BUFFER];
    memset(&online,'\0',sizeof(online));
    int flag=0,a,b;
    while(1)
    {
        int numBytesRead = read(clientSocketFd, msgBuffer, MAX_BUFFER - 1);
        msgBuffer[numBytesRead] = '\0';

        //If the client sent /exit\n, remove them from the client list and close their socket
        if(strcmp(msgBuffer, "/exit\n") == 0)
        {
            fprintf(stderr, "Client on socket %d has disconnected.\n", clientSocketFd);
            removeClient(data, clientSocketFd);
            return NULL;
        }
	else if(strcmp(msgBuffer, "/list\n")==0)
	{
		listClient(data,online,clientSocketFd);
		memset(&online,'\0',sizeof(online));
	}
	else if(strncmp(msgBuffer, "/to",3)==0)
	{
		strcpy(copy,msgBuffer);
		ptr=strtok(copy," ");
		ptr=strtok(NULL," ");
		send_to(data,ptr,msgBuffer,clientSocketFd);	
		memset(&copy,'\0',sizeof(copy));
	}
	else if(strncmp(msgBuffer, "/file",5)==0)
	{	
		read(clientSocketFd,file,MAX_BUFFER-1);
		strcat(f,file);
		strcpy(copy,msgBuffer);
		ptr=strtok(copy," ");
		ptr=strtok(NULL," ");	//ptr=receiver
		file_to(data,ptr,msgBuffer,clientSocketFd,f);
		memset(&copy,'\0',sizeof(copy));
	}
        else
        {
            //Wait for queue to not be full before pushing message
            while(q->full)
            {
                pthread_cond_wait(q->notFull, q->mutex);
            }

            //Obtain lock, push message to queue, unlock, set condition variable
            pthread_mutex_lock(q->mutex);
            fprintf(stderr, "Pushing message to queue: %s\n", msgBuffer);
            queuePush(q, msgBuffer);
            pthread_mutex_unlock(q->mutex);
            pthread_cond_signal(q->notEmpty);
        }
    }
}

//The "consumer" -- waits for the queue to have messages then takes them out and broadcasts to clients
void *messageHandler(void *data)
{
    chatDataVars *chatData = (chatDataVars *)data;
    queue *q = chatData->queue;
    int *clientSockets = chatData->clientSockets;
    char *ptr,*qtr;
    char copy[1024],from[50]=" from ";
    while(1)
    {
        //Obtain lock and pop message from queue when not empty
        pthread_mutex_lock(q->mutex);
        while(q->empty)
        {
            pthread_cond_wait(q->notEmpty, q->mutex);
        }
        char* msg = queuePop(q);
        pthread_mutex_unlock(q->mutex);
        pthread_cond_signal(q->notFull);

        //Broadcast message to all connected clients
        fprintf(stderr, "Broadcasting message: %s\n", msg);
/*	strcpy(copy,msg);
	ptr=strtok(copy,":");
	ptr=strtok(NULL," ");
	qtr=strtok(NULL," ");
*/      for(int i = 0; i < chatData->numClients; i++)
        {
            int socket = clientSockets[i];
	/*    if(strcmp(ptr,"/to")==0 && strcmp(qtr,chatData->name[socket])==0){
		    ptr=strtok(NULL,"\n");
		    strcat(from,copy);
		    strcat(ptr,from);
		    strcat(ptr,"\n");
		    fprintf(stderr,"ptr = %s",ptr);
		    if(socket!=0 && write(socket,ptr,MAX_BUFFER-1)==-1)
			    perror("Socket write failed: ");
	    }*/
	    if(socket != 0 && write(socket, msg, MAX_BUFFER - 1) == -1)
                perror("Socket write failed: ");
        }
    }
}
