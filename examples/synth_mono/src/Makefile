CC = gcc
CFLAGS = -fPIC -Wall -O0 -g -I../../../tibia/ -I../../../../../Orastron/repos/brickworks/include
LDFLAGS = -shared
TARGET = ../plugin.so
SRC = plugin.c

all:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)