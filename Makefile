CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -g -pthread -D_POSIX_C_SOURCE=200809L
LIBS = -lpthread -ljson-c

SRC = src
INC = inc
BUILD = build

SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(SRCS))
TARGET = $(BUILD)/server

.PHONY: all clean run

all: $(BUILD) $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LIBS)
	@echo "✓ Done!"

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -I$(INC) -c $< -o $@

clean:
	rm -rf $(BUILD)

run: $(TARGET)
	./$(TARGET)

