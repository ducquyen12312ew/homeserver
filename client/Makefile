CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g $(shell pkg-config --cflags gtk+-3.0)
LIBS = $(shell pkg-config --libs gtk+-3.0) -ljson-c

SRC = src/main.c
TARGET = build/client

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)
	@echo "Client build complete!"

clean:
	rm -rf build

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
