CC=gcc
CFLAGS=-g -Wall
LDFLAGS=-libverbs	-lm

all: compute_node logstore

compute_node: compute_node.c rdma.c rdma.h
	$(CC) $(CFLAGS) -o compute_node compute_node.c rdma.c $(LDFLAGS)

logstore: logstore.c rdma.c rdma.h
	$(CC) $(CFLAGS) -o logstore logstore.c rdma.c $(LDFLAGS)

clean:
	rm -f compute_node logstore