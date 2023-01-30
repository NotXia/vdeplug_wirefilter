#ifndef INCLUDE_QUEUE
#define INCLUDE_QUEUE
#include "./wf_conn.h"

void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet);
Packet *dequeue(struct vde_wirefilter_conn *vde_conn);
Packet *queue_head(struct vde_wirefilter_conn *vde_conn);

void setTimer(struct vde_wirefilter_conn *vde_conn);

#endif