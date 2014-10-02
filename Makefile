CC = gcc

all:
	$(CC) -o server server.c
	$(CC) -o client client.c

clean:
	rm server client