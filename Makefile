CC = gcc
CFLAGS = -Wall -g
OBJS_OSS = oss.o
OBJS_WORKER = worker.o

all: oss worker

oss: $(OBJS_OSS)
	$(CC) $(CFLAGS) -o oss $(OBJS_OSS)

worker: $(OBJS_WORKER)
	$(CC) $(CFLAGS) -o worker $(OBJS_WORKER)

# Suffix rule for .c to .o
.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o oss worker

.PHONY: all clean
