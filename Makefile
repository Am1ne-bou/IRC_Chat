CC = gcc
CFLAGS = -Wall 
TARGETS = server client
LDFLAGS =

all: $(TARGETS)

client: client.c common.h msg_struct.h
	$(CC) $(CFLAGS) -o $@ client.c $(LDFLAGS)

server: server.c user_manager.c history_manager.c common.h msg_struct.h user_manager.h history_manager.h
	$(CC) $(CFLAGS) -o $@ server.c user_manager.c history_manager.c $(LDFLAGS)

clean:
	rm -f $(TARGETS)


fclean: clean
	rm -f users.txt
	rm -rf .resa1

.PHONY: all clean