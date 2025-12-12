CC = gcc

CFLAGS = -Wall -O2
LIBS = -ldl
TARGET = build/loader_test
SRC = loader_test.c loader.c

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)