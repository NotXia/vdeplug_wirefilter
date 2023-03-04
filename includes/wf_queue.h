#ifndef INCLUDE_QUEUE
#define INCLUDE_QUEUE

#include <stdint.h>

struct vde_wirefilter_conn;
struct packet_t;
typedef struct packet_t Packet;


struct queuenode_t {
	Packet *packet;
	uint64_t forward_time;
	unsigned int counter;
};
typedef struct queuenode_t QueueNode;


void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet, uint64_t forward_time);
Packet *dequeue(struct vde_wirefilter_conn *vde_conn);
uint64_t nextQueueTime(struct vde_wirefilter_conn *vde_conn);

void setTimer(struct vde_wirefilter_conn *vde_conn);

#endif