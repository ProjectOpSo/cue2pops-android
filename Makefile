CC = clang
CFLAGS = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64
TARGET = cue2pops
SRCS = cue2pops.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
