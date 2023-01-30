#include "./wf_queue.h"
#include <sys/timerfd.h>
#include "./wf_time.h"


/**
 * 
 * !! Currently with a temporary implementation !!
 * 
*/

void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
	int i=0;
	while (i < vde_conn->queue_size && vde_conn->queue[i]->forward_time <= packet->forward_time) { i++; }

	for (int j=vde_conn->queue_size-1; j>=i; j--) { vde_conn->queue[j+1] = vde_conn->queue[j]; }
	vde_conn->queue[i] = packet;
	vde_conn->queue_size++;
}

Packet *dequeue(struct vde_wirefilter_conn *vde_conn) {
	if (vde_conn->queue_size == 0) { return NULL; }
	Packet *packet;

	packet = vde_conn->queue[0];
	
	for (int i=0; i<vde_conn->queue_size-1; i++) {
		vde_conn->queue[i] = vde_conn->queue[i+1];
	}
	vde_conn->queue_size--;

	return packet;
}

Packet *queue_head(struct vde_wirefilter_conn *vde_conn) {
	if (vde_conn->queue_size == 0) { return NULL; }
	return vde_conn->queue[0];
}


/* Sets the timerfd for the next packet to send */
void setTimer(struct vde_wirefilter_conn *vde_conn) {
	long long next_time_step = queue_head(vde_conn)->forward_time - now_ns();
	if (next_time_step <= 0) next_time_step = 1;

	time_t seconds = next_time_step / (1000000000);
	long nseconds = next_time_step % (1000000000);
	struct itimerspec next = { { 0, 0 }, { seconds, nseconds } };
	
	timerfd_settime(vde_conn->queue_timer, 0, &next, NULL);
}