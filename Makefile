CFLAGS?=	-Wall -g -W -O
CC?=	gcc
VERSION=1.6

all:	mywebbench

mywebbench: mywebbench.o socket.o
	$(CC) $(CFLAGES) -o mywebbench mywebbench.o

mywebbench.o: mywebbench.cpp

socket.o: socket.cpp

clean:
	rm -f *.o mywebbench