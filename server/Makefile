CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -pthread -D_POSIX_C_SOURCE=200809L
LIBS = -lpthread -ljson-c
INC = -Iinc

SRC = src/protocol.c src/server.c src/main.c
OBJ = $(SRC:src/%.c=build/%.o)
TARGET = build/server

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LIBS)
	@echo "Server build complete!"

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

clean:
	rm -rf build

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
