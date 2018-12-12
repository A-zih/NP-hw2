#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<errno.h>
#include<netdb.h>
#include<sys/select.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<fcntl.h>
#include<signal.h>

static int sockfd;
void buildMessage(char *result,char *name,char *msg){
	memset(result,0,1024);
	strcpy(result,name);
	strcat(result,": ");
	strcat(result,msg);
}

void setNonBlock(int fd){
	int flags=fcntl(fd,F_GETFL);
	if(flags<0)
		perror("fcntl failed.");
	flags |= O_NONBLOCK;
	fcntl(fd,F_SETFL,flags);
}

void interruptHandler(int sig_unused){
	if(write(sockfd, "/exit\n",1024-1)==-1)
		perror("write failed.");
	close(sockfd);
	exit(1);
}
void interruptHandler2(int sig_unused){
	if(write(sockfd, "/list\n",1024-1)==-1)
		perror("write failed.");

}
void interruptHandler3(int sig_unused,char *chatBuff){
	if(write(sockfd,chatBuff,1024-1)==-1)
		perror("write failed.");
}
void interruptHandler4(int sig_unused,char *chatBuff){
	char *ptr,fd;
	char cp_msg[1024];
	strcpy(cp_msg,chatBuff);
	ptr=strtok(cp_msg," ");
	ptr=strtok(NULL," ");
	ptr=strtok(NULL,"\n");	//ptr = file name
	fd=open(ptr,O_RDONLY);
	if(fd==-1)
		perror("file can't find");
	else{
		if(write(sockfd,chatBuff,1024-1)==-1)
			perror("write failed.");
		sendfile(sockfd,fd,NULL,1024-1);
	}
}
int main(){
	int flag=0,a;
	char name[100],*ptr;
	char buf[1024];
	struct sockaddr_in servaddr;

	printf("Enter user name: ");
	scanf("%s",name);
	
	sockfd=socket(AF_INET,SOCK_STREAM,0);
	if(sockfd==-1)
		perror("Could not create socket.\n");
	
	printf("Create Socket successfully.\n");
	printf("You can start chatting now!\n");
	bzero(&servaddr,sizeof(servaddr));
	servaddr.sin_family=AF_INET;
	servaddr.sin_port=htons(8888);
	servaddr.sin_addr.s_addr=inet_addr("127.0.0.1");
	if(connect(sockfd,(struct sockaddr *) &servaddr,sizeof(struct sockaddr))<0){
		perror("Couldn't connect to server.\n");
		exit(1);
	}

	if(write(sockfd,name,99)==-1)
		perror("wrtie name failed.");

	fd_set clientFds;
	setNonBlock(sockfd);
	setNonBlock(0);
	char chatMsg[1024];
	char chatBuffer[1024],msgBuffer[1024];

	while(1){
		FD_ZERO(&clientFds);
		FD_SET(sockfd,&clientFds);
		FD_SET(0,&clientFds);
		if(select(FD_SETSIZE,&clientFds,NULL,NULL,NULL)!=-1){
			for(int fd=0;fd<FD_SETSIZE;fd++){
				if(FD_ISSET(fd,&clientFds)){
					if(fd==sockfd){
						int numBytesRead=read(sockfd,msgBuffer,1024-1);
						msgBuffer[numBytesRead]='\0';
						if(strncmp("#FILE",msgBuffer,5)==0){
							char file_name[1024],content[1024];
							read(sockfd,file_name,1024-1);
							int i=0;
							while(file_name[i]!='\n')
								i++;
							file_name[i]='\0';
							FILE *fp=fopen(file_name,"w");
							strcpy(content,msgBuffer);
							ptr=content;
							ptr=ptr+5;
							fwrite(ptr,1,strlen(ptr),fp);
							fclose(fp);
							printf("Receiving file successfully!\n");
						}
						else
							printf("%s",msgBuffer);
						memset(&msgBuffer,0,sizeof(msgBuffer));
					}
					else if(fd==0){
						fgets(chatBuffer,1024-1,stdin);
						if(strcmp(chatBuffer,"/exit\n")==0)
							interruptHandler(-1);
						else if(strcmp(chatBuffer,"/list\n")==0){
							interruptHandler2(-2);
							a=read(sockfd,buf,1024-1);
							buf[a]='\0';
							printf("%s",buf);
							memset(&buf,0,sizeof(buf));
						}
						else if(strncmp(chatBuffer,"/to",3)==0){
							interruptHandler3(-3,chatBuffer);
						}
						else if(strncmp(chatBuffer,"/file",5)==0){
							interruptHandler4(-4,chatBuffer);
						}
						else{
							buildMessage(chatMsg,name,chatBuffer);
							if(write(sockfd,chatMsg,1024-1)==-1)
								perror("write failed\n");
							memset(&chatBuffer,0,sizeof(chatBuffer));
						}
					}
				}
			}
		}
	}

	return 0;
}
