CC = gcc

CFLAGS = -Wall -O2
LIBS = -ldl
TARGET = build/main
SRC = main.c loader.c

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)