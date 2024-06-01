CC := gcc
CFLAGS = -lpthread -Wall

files := server
path := utils/
utils := $(path)dict.c $(path)parser.c $(path)timing.c

all: $(files)

# $(files): % : %.c
# 	$(CC) -o ./build/$@ $^ $(CFLAGS)

# server: server.c $(path)queue.c utils/list.c utils/servUtils.c
# 	$(CC) -o ./build/$@ $^ $(CFLAGS)
server: server.c $(utils)
	$(CC) -o ./$@.exe $^ $(CFLAGS)

# client: client.c
# 	$(CC) -o ./build/$@ $^ $(CFLAGS)