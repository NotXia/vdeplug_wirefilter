#ifndef INCLUDE_QUEUE
#define INCLUDE_QUEUE

struct vde_wirefilter_conn;
struct packet_t;
typedef struct packet_t Packet;


struct queuenode_t {
	Packet *packet;
	unsigned long long forward_time;
	unsigned int counter;
};
typedef struct queuenode_t QueueNode;


void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet, unsigned long long forward_time);
Packet *dequeue(struct vde_wirefilter_conn *vde_conn);
unsigned long long nextQueueTime(struct vde_wirefilter_conn *vde_conn);

void setTimer(struct vde_wirefilter_conn *vde_conn);

#endif