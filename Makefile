CC = gcc

# Standard flags + pthread for miniaudio
CFLAGS = -Wall -O0 -g -pthread -I../../Orastron/repos/miniaudio
# Linker flags: dynamic loader and math
LIBS = -ldl -lm

# Build output folder
BUILD_DIR = build
TARGET = $(BUILD_DIR)/host

SRC = main.c loader.c

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -rf $(BUILD_DIR)