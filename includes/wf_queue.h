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


int initQueue(struct vde_wirefilter_conn *vde_conn, const char fifoness);
void closeQueue(struct vde_wirefilter_conn *vde_conn);

void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet, uint64_t forward_time);
Packet *dequeue(struct vde_wirefilter_conn *vde_conn);
uint64_t nextQueueTime(struct vde_wirefilter_conn *vde_conn);

void setQueueTimer(struct vde_wirefilter_conn *vde_conn);

#endif