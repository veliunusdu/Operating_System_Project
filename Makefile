CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
LIBS    = -lpthread -lrt

TARGET  = ferry_sim
SRCS    = ferry_sim.c
HDRS    = ferry_sim.h

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
