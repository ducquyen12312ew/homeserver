CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -Iinc $(shell pkg-config --cflags gtk+-3.0)
LIBS = $(shell pkg-config --libs gtk+-3.0) -ljson-c
SRC_DIR = src
BUILD_DIR = build
SOURCES = $(SRC_DIR)/main.c $(SRC_DIR)/message_builder.c $(SRC_DIR)/network_helper.c
OBJECTS = $(BUILD_DIR)/main.o $(BUILD_DIR)/message_builder.o $(BUILD_DIR)/network_helper.o
TARGET = $(BUILD_DIR)/client

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run

