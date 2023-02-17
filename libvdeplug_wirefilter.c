/*
 * VDE - libvdeplug_wirefilter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <libvdeplug.h>
#include <libvdeplug_mod.h>
#include <pthread.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <wf_conn.h>
#include <wf_queue.h>
#include <wf_time.h>
#include <wf_markov.h>
#include "./includes/wf_debug.h"


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args);
static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags);
static ssize_t vde_wirefilter_send(VDECONN *conn, const void *buf, size_t len, int flags);
static int vde_wirefilter_datafd(VDECONN *conn);
static int vde_wirefilter_ctlfd(VDECONN *conn);
static int vde_wirefilter_close(VDECONN *conn);


// Module structure
struct vdeplug_module vdeplug_ops = {
	.vde_open_real = vde_wirefilter_open,
	.vde_recv = vde_wirefilter_recv,
	.vde_send = vde_wirefilter_send,
	.vde_datafd = vde_wirefilter_datafd,
	.vde_ctlfd = vde_wirefilter_ctlfd,
	.vde_close = vde_wirefilter_close
};

static void *packetHandlerThread(void *param);
static void handlePacket(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static void sendPacket(struct vde_wirefilter_conn *vde_conn, const Packet *packet);


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void)descr; (void)interface_version; (void)open_args;

	struct vde_wirefilter_conn *newconn = NULL;
	VDECONN *conn;
	char *nested_vnl;
	char *delay_str[3] = { NULL, NULL, NULL };
	char *dup_str[3] = { NULL, NULL, NULL };
	char *loss_str[3] = { NULL, NULL, NULL };
	char *bursty_loss_str = NULL;
	char *mtu_str = NULL;
	struct vdeparms parms[] = {
		{ "delay", &delay_str[BIDIRECTIONAL] }, { "delayLR", &delay_str[LEFT_TO_RIGHT] }, { "delayRL", &delay_str[RIGHT_TO_LEFT] },
		{ "dup", &dup_str[BIDIRECTIONAL] }, { "dupLR", &dup_str[LEFT_TO_RIGHT] }, { "dupRL", &dup_str[RIGHT_TO_LEFT] },
		{ "loss", &loss_str[BIDIRECTIONAL] }, { "lossLR", &loss_str[LEFT_TO_RIGHT] }, { "lossRL", &loss_str[RIGHT_TO_LEFT] },
		{ "lostburst", &bursty_loss_str },
		{ "mtu", &mtu_str },
		{ NULL, NULL }
	};

	nested_vnl = vde_parsenestparms(vde_url);							// Gets the nested VNL
	if ( vde_parsepathparms(vde_url, parms) != 0 ) { return NULL; } 	// Retrieves the plugin parameters
	
	// Opens the connection with the nested VNL
	conn = vde_open(nested_vnl, descr, open_args);
	if (conn == NULL) { return NULL; }
	if ( (newconn = calloc(1, sizeof(struct vde_wirefilter_conn))) == NULL ) {
		errno = ENOMEM;
		goto error;
	}
	newconn->conn=conn;

	// Pipes initialization
	newconn->send_pipefd = malloc(2*sizeof(int));
	newconn->receive_pipefd = malloc(2*sizeof(int));
	if ( pipe(newconn->send_pipefd) != 0 ) { goto error; } 
	if ( pipe(newconn->receive_pipefd) != 0 ) { goto error; } 


	// Starts packet handler thread
	pthread_t packet_handler_thread;
	if ( pthread_create(&packet_handler_thread, NULL, &packetHandlerThread, (void*)newconn) != 0 ) { goto error; };
	newconn->packet_handler_thread = packet_handler_thread;

	pthread_mutex_init(&newconn->receive_lock, NULL);


	newconn->fifoness = FIFO;
	
	newconn->queue_timer = timerfd_create(CLOCK_REALTIME, 0);
	newconn->queue.queue = NULL;
	newconn->queue.size = 0;
	newconn->queue.max_size = 0;
	newconn->queue.byte_size[LEFT_TO_RIGHT] = 0;
	newconn->queue.byte_size[RIGHT_TO_LEFT] = 0;
	newconn->queue.max_forward_time = 0;
	newconn->queue.counter = 0;


	markov_init(newconn);
	setWireValue(MARKOV_CURRENT(newconn), DELAY, delay_str[BIDIRECTIONAL], delay_str[LEFT_TO_RIGHT], delay_str[RIGHT_TO_LEFT]);
	setWireValue(MARKOV_CURRENT(newconn), DUP, dup_str[BIDIRECTIONAL], dup_str[LEFT_TO_RIGHT], dup_str[RIGHT_TO_LEFT]);
	setWireValue(MARKOV_CURRENT(newconn), LOSS, loss_str[BIDIRECTIONAL], loss_str[LEFT_TO_RIGHT], loss_str[RIGHT_TO_LEFT]);
	setWireValue(MARKOV_CURRENT(newconn), BURSTYLOSS, bursty_loss_str, NULL, NULL);
	setWireValue(MARKOV_CURRENT(newconn), MTU, mtu_str, NULL, NULL);


	return (VDECONN *)newconn;

	error:
		vde_close(conn);
		return NULL;
}

// Right to left
static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags) {
	/* 
		Note: Packets from the nested plugin (right side) are intercepted by the packet handler thread first
	*/
	(void)len; (void)flags;
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	const int POLL_SIZE = 1;
	struct pollfd poll_fd[1] = {
		{ .fd=vde_conn->receive_pipefd[0], .events=POLLIN }
	};
	int poll_ret;

	if ( (poll_ret = poll(poll_fd, POLL_SIZE, -1)) > 0 ) {
		// A packet arrived from the thread (so it can be received)
		if (poll_fd[0].revents & POLLIN) {
			ssize_t read_len = read(vde_conn->receive_pipefd[0], buf, VDE_ETHBUFSIZE);
			pthread_mutex_unlock(&vde_conn->receive_lock);
			
			if (__builtin_expect(read_len < 0, 0)) { goto error; }

			return read_len;
		}
	}

	error:
		errno = EAGAIN;
		return 1;
}

// Left to right
static ssize_t vde_wirefilter_send(VDECONN *conn, const void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	Packet *packet = malloc(sizeof(Packet));
	if (__builtin_expect(packet == NULL, 0)) { goto error; }
	packet->buf = malloc(len);
	if (__builtin_expect(packet->buf == NULL, 0)) { goto error; }

	memcpy(packet->buf, buf, len);
	packet->len = len;
	packet->flags = flags;
	packet->direction = LEFT_TO_RIGHT;

	// Passes the packet to the handler
	if (__builtin_expect( write(vde_conn->send_pipefd[1], (void*)&packet, sizeof(void*)) < 0, 0 )) { 
		goto error; 
	}

	return 0;

	error:
		errno = EAGAIN;
		return -1;
}

static int vde_wirefilter_datafd(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_conn->receive_pipefd[0];
}

static int vde_wirefilter_ctlfd(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_ctlfd(vde_conn->conn);
}

static int vde_wirefilter_close(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	pthread_cancel(vde_conn->packet_handler_thread);
	close(vde_conn->send_pipefd[0]);
	close(vde_conn->send_pipefd[1]);
	close(vde_conn->receive_pipefd[0]);
	close(vde_conn->receive_pipefd[1]);
	close(vde_conn->queue_timer);
	pthread_mutex_destroy(&vde_conn->receive_lock);
	
	int ret_value = vde_close(vde_conn->conn);
	free(vde_conn);
	return ret_value;
}


static void *packetHandlerThread(void *param) {
	pthread_detach(pthread_self());

	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)param;
	
	const int POLL_SIZE = 3;
	struct pollfd poll_fd[3] = {
		{ .fd=vde_conn->send_pipefd[0], .events=POLLIN },	// Left to right packets
		{ .fd=vde_datafd(vde_conn->conn), .events=POLLIN },	// Right to left packets
		{ .fd=vde_conn->queue_timer, .events=POLLIN }		// Packet queue timer
	};
	int poll_ret;

	ssize_t rw_len;

	unsigned char receive_buffer[VDE_ETHBUFSIZE];
	ssize_t receive_length;


	while(1) {
		poll_ret = poll(poll_fd, POLL_SIZE, -1);

		if (poll_ret > 0) {
			// A packet has to be sent
			if (poll_fd[0].revents & POLLIN) {
				Packet *packet;
				rw_len = read(vde_conn->send_pipefd[0], &packet, sizeof(void*));
				if (__builtin_expect(rw_len < 0, 0)) { errno = EAGAIN; }

				handlePacket(vde_conn, packet);
				free(packet);
			}

			// A packet can be received from the nested plugin
			if (poll_fd[1].revents & POLLIN) {
				receive_length = vde_recv(vde_conn->conn, receive_buffer, VDE_ETHBUFSIZE, 0);
				if (receive_length == 1) { continue; } // Discarded

				Packet *packet = malloc(sizeof(Packet));
				packet->buf = malloc(receive_length);
				memcpy(packet->buf, receive_buffer, receive_length);
				packet->len = receive_length;
				packet->flags = 0;
				packet->direction = RIGHT_TO_LEFT;

				handlePacket(vde_conn, packet);
				free(packet);
			}

			// Time to send something
			if (poll_fd[2].revents & POLLIN) {
				struct itimerspec disarm_timer = { { 0, 0 }, { 0, 0 } };
				timerfd_settime(vde_conn->queue_timer, 0, &disarm_timer, NULL);

				Packet *packet;

				while(vde_conn->queue.size > 0 && nextQueueTime(vde_conn) < now_ns()) {
					packet = dequeue(vde_conn);
					sendPacket(vde_conn, packet);
					free(packet);
				}

				// Sets the timer for the next packet
				if (vde_conn->queue.size > 0) {
					setTimer(vde_conn);
				}
			}
			
		}
	}

	pthread_exit(0);
}


static void handlePacket(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	/* MTU handling */
	if (minWireValue(MARKOV_CURRENT(vde_conn), MTU, packet->direction) > 0 && packet->len > minWireValue(MARKOV_CURRENT(vde_conn), MTU, packet->direction)) {
		return;
	}


	/* Loss handling */

	// Total loss
	if ( minWireValue(MARKOV_CURRENT(vde_conn), LOSS, packet->direction) >= 100.0 ) { return; }

	if (maxWireValue(MARKOV_CURRENT(vde_conn), BURSTYLOSS, packet->direction) > 0) {
		// Loss with Gilbert model
		double loss_val = computeWireValue(MARKOV_CURRENT(vde_conn), LOSS, packet->direction) / 100;
		double burst_len = computeWireValue(MARKOV_CURRENT(vde_conn), BURSTYLOSS, packet->direction);

		switch (vde_conn->bursty_loss_status[packet->direction]) {
			case OK_BURST:
				if ( drand48() < (loss_val / (burst_len*(1-loss_val))) ) { 
					vde_conn->bursty_loss_status[packet->direction] = FAULTY_BURST; 
				}
				break;
			case FAULTY_BURST:
				if ( drand48() < (1.0 / burst_len) ) { 
					vde_conn->bursty_loss_status[packet->direction] = OK_BURST; 
				}
				break;
		}

		if (vde_conn->bursty_loss_status[packet->direction] != OK_BURST) { return; }
	}
	else {
		vde_conn->bursty_loss_status[packet->direction] = OK_BURST;
		
		// Standard loss handling
		if (drand48() < (computeWireValue(MARKOV_CURRENT(vde_conn), LOSS, packet->direction) / 100)) {
			return;
		}
	}


	double delay = 0;
	int send_times = 1;

	/* Computes the number of duplicates */
	if (maxWireValue(MARKOV_CURRENT(vde_conn), DUP, packet->direction) > 0) {
		while (drand48() < (computeWireValue(MARKOV_CURRENT(vde_conn), DUP, packet->direction) / 100)) { send_times++; }
	}

	for (int i=0; i<send_times; i++) {
		delay = 0;

		/* Packet delay */
		delay = computeWireValue(MARKOV_CURRENT(vde_conn), DELAY, packet->direction);

		if (delay > 0) {
			Packet *packet_copy = packetCopy(packet);

			enqueue(vde_conn, packet_copy, now_ns() + MS_TO_NS(delay));
			setTimer(vde_conn);
		}
		else {
			sendPacket(vde_conn, packet);
		}
	}
}


/* Sends the packet to the correct destination */
static void sendPacket(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	if (packet->direction == LEFT_TO_RIGHT) {
		vde_send(vde_conn->conn, packet->buf, packet->len, packet->flags);
	}
	else {
		ssize_t rw_len;

		pthread_mutex_lock(&vde_conn->receive_lock);
		// Makes the packet receivable
		rw_len = write(vde_conn->receive_pipefd[1], packet->buf, packet->len);
		if (__builtin_expect(rw_len < 0, 0)) { errno = EAGAIN; }
	}
}