all:client server
	
client:client.c
	gcc -o client client.c

server:server.c
	gcc -pthread -o server server.c

clean: rm -f client server

