#include "./wf_queue.h"
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include "./wf_conn.h"
#include "./wf_time.h"
#include "./wf_log.h"

#define QUEUE_CHUNK 100


int initQueue(struct vde_wirefilter_conn *vde_conn, const char fifoness) {
	vde_conn->queue.fifoness = fifoness;
	vde_conn->queue.timerfd = timerfd_create(CLOCK_REALTIME, 0);
	handle_error(vde_conn->queue.timerfd < 0, { return -1; }, "Queue timer fd init error: %s", strerror(errno));
	vde_conn->queue.queue = NULL;
	vde_conn->queue.size = 0;
	vde_conn->queue.max_size = 0;
	vde_conn->queue.byte_size[LEFT_TO_RIGHT] = 0;
	vde_conn->queue.byte_size[RIGHT_TO_LEFT] = 0;
	vde_conn->queue.max_forward_time = 0;
	vde_conn->queue.counter = 0;
	
	return 0;
}

void closeQueue(struct vde_wirefilter_conn *vde_conn) {
	free(vde_conn->queue.queue);
	close(vde_conn->queue.timerfd);
}


static void resizeQueue(struct vde_wirefilter_conn *vde_conn, const int new_size) {
	if (vde_conn->queue.queue == NULL) {
		vde_conn->queue.queue = malloc(new_size * sizeof(QueueNode*));
		handle_error( vde_conn->queue.queue == NULL, { exit(1); }, "Queue malloc error" );

		if (new_size > 0) {
			QueueNode *sentinel = malloc(sizeof(QueueNode));
			handle_error( sentinel == NULL, { exit(1); }, "Queue sentinel malloc error" );
			sentinel->packet = NULL;
			sentinel->forward_time = 0;
			sentinel->counter = 0;
			vde_conn->queue.queue[0] = sentinel;
		}
	}
	else {
		vde_conn->queue.queue = realloc(vde_conn->queue.queue, new_size * sizeof(QueueNode*));
		handle_error( vde_conn->queue.queue == NULL, { exit(1); }, "Queue realloc error" );
	}
	
	vde_conn->queue.max_size = new_size;
}

/*
	Returns:
	 1 if node1 > node2
	 0 if node1 == node2
	-1 if node1 < node2
*/
static int compareNode(const QueueNode *node1, const QueueNode *node2) {
	if (node1->forward_time == node2->forward_time && node1->counter == node2->counter) { return 0; }

	if ( (node1->forward_time < node2->forward_time) || (node1->forward_time == node2->forward_time && node1->counter < node2->counter) ) {
		return -1;
	}
	else {
		return 1;
	}
}

void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet, uint64_t forward_time) {
	QueueNode *new = malloc(sizeof(QueueNode));
	handle_error( new == NULL, { exit(1); }, "Queue new node malloc error" );

	// Handle ordering for fifoness
	if (vde_conn->queue.fifoness == FIFO) {
		if (forward_time > vde_conn->queue.max_forward_time) {
			// This packet has to be sent later than any of the current packets in the queue
			// All future packets will be sent after this one (even if they should be sent before)
			vde_conn->queue.max_forward_time = forward_time;
			vde_conn->queue.counter = 0;
		}
		else {
			// There is at least a packet that arrived before but has to be sent later than this one 
			// This packet has to wait for the previous one
			forward_time = vde_conn->queue.max_forward_time;
			vde_conn->queue.counter++;
		}
	}

	new->packet = packet;
	new->forward_time = forward_time;
	new->counter = vde_conn->queue.counter;

	// Queue resize
	if (vde_conn->queue.size >= vde_conn->queue.max_size) {
		resizeQueue(vde_conn, vde_conn->queue.max_size + QUEUE_CHUNK);
	}

	vde_conn->queue.size++;
	vde_conn->queue.byte_size[packet->direction] += packet->len;

	// Adds new node to heap
	int k = vde_conn->queue.size;
	while ( compareNode(new, vde_conn->queue.queue[k>>1]) < 0 ) {
		vde_conn->queue.queue[k] = vde_conn->queue.queue[k>>1];
		k >>= 1;
	}
	vde_conn->queue.queue[k] = new;
}

Packet *dequeue(struct vde_wirefilter_conn *vde_conn) {
	if (vde_conn->queue.size <= 0) { return NULL; }
	
	Packet *out_packet = vde_conn->queue.queue[1]->packet;

	// Head remove
	vde_conn->queue.size--;
	vde_conn->queue.byte_size[out_packet->direction] -= out_packet->len;
	free(vde_conn->queue.queue[1]);

	// Heap rebuild
	QueueNode *old = vde_conn->queue.queue[vde_conn->queue.size+1];
	unsigned int k = 1;

	while (k <= vde_conn->queue.size/2) {
		unsigned int j = k<<1;

		// Selects the min between queue[2k] and queue[2k+1]
		if ((j < vde_conn->queue.size) && compareNode(vde_conn->queue.queue[j], vde_conn->queue.queue[j+1]) > 0) { j++; }

		if (compareNode(old, vde_conn->queue.queue[j]) < 0) { 
			break; 
		}
		else {
			vde_conn->queue.queue[k] = vde_conn->queue.queue[j];
			k = j;
		}
	}
	vde_conn->queue.queue[k] = old;

	return out_packet;
}

uint64_t nextQueueTime(struct vde_wirefilter_conn *vde_conn) {
	return vde_conn->queue.queue[1]->forward_time;
}


/* Sets the timerfd for the next packet to send */
void setQueueTimer(struct vde_wirefilter_conn *vde_conn) {
	int64_t next_time_step = nextQueueTime(vde_conn) - now_ns();
	if (next_time_step <= 0) next_time_step = 1;

	setTimer(vde_conn->queue.timerfd, next_time_step);
}