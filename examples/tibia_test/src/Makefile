CC = gcc
CFLAGS = -fPIC -Wall -O2 -I../../../tibia/
LDFLAGS = -shared
TARGET = ../plugin.so
SRC = plugin.c

all:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)