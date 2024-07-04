compute_node: compute_node.c rdma.c
	cc compute_node.c rdma.c -o compute_node -g -libverbs -lpthread

logstore: logstore.c rdma.c
	cc logstore.c rdma.c -o logstore -g -libverbs -lpthread

clean:
	rm -rf ./*.o ./compute_node ./logstore