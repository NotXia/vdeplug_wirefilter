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
#include <time.h>
#include <libvdeplug.h>
#include <libvdeplug_mod.h>
#include <pthread.h>
#include <poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1

#define MS_TO_NS(ms) ((ms) * 1000000)

typedef struct {
	void *buf;
	size_t len;
	int flags;
	int direction;

	unsigned long long forward_time; 
} Packet;

static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args);
static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags);
static ssize_t vde_wirefilter_send(VDECONN *conn, const void *buf, size_t len, int flags);
static int vde_wirefilter_datafd(VDECONN *conn);
static int vde_wirefilter_ctlfd(VDECONN *conn);
static int vde_wirefilter_close(VDECONN *conn);

// Connection structure of the module
struct vde_wirefilter_conn {
	void *handle;
	struct vdeplug_module *module;

	VDECONN *conn;
	
	pthread_t packet_handler_thread;
	int *send_pipefd;
	int *receive_pipefd;
	pthread_mutex_t receive_lock; // Mutex to prevent concurrent writes on the receive pipe

	int queue_timer; // Timer for packets delay

	Packet* queue[10000];
	int queue_size;

	long delay_lr, delay_rl;
};

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
static void handlePacket(struct vde_wirefilter_conn *vde_conn, Packet *packet);
static void sendPacket(struct vde_wirefilter_conn *vde_conn, Packet *packet);

static unsigned long long now_us();
static void setTimer(struct vde_wirefilter_conn *vde_conn);

static void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet);
static Packet *dequeue(struct vde_wirefilter_conn *vde_conn);
static Packet *queue_head(struct vde_wirefilter_conn *vde_conn);


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void)descr; (void)interface_version; (void)open_args;

	struct vde_wirefilter_conn *newconn = NULL;
	VDECONN *conn;
	char *nested_vnl;
	char *delay_str = NULL;
	char *delay_lr_str = NULL;
	char *delay_rl_str = NULL;
	struct vdeparms parms[] = {
		{ "delay", &delay_str },
		{ "delayLR", &delay_lr_str },
		{ "delayRL", &delay_rl_str },
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

	newconn->delay_lr = delay_str ? atol(delay_str) : 0;
	newconn->delay_rl = delay_str ? atol(delay_str) : 0;
	if (delay_lr_str) { newconn->delay_lr = atol(delay_lr_str); }
	if (delay_rl_str) { newconn->delay_rl = atol(delay_rl_str); }

	newconn->queue_timer = timerfd_create(CLOCK_REALTIME, 0);
	newconn->queue_size = 0;


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
			}

			// Time to send something
			if (poll_fd[2].revents & POLLIN) {
				struct itimerspec disarm_timer = { { 0, 0 }, { 0, 0 } };
				timerfd_settime(vde_conn->queue_timer, 0, &disarm_timer, NULL);

				Packet *packet;

				while(vde_conn->queue_size > 0 && queue_head(vde_conn)->forward_time < now_us()) {
					packet = dequeue(vde_conn);
					sendPacket(vde_conn, packet);
					free(packet);
				}

				// Sets the timer for the next packet
				if (vde_conn->queue_size > 0) {
					setTimer(vde_conn);
				}
			}
			
		}
	}

	pthread_exit(0);
}


/* Sends the packet to the correct destination */
static void sendPacket(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
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

static void handlePacket(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
	long delay = packet->direction == LEFT_TO_RIGHT ? vde_conn->delay_lr : vde_conn->delay_rl;

	if (delay > 0) {
		packet->forward_time = now_us() + MS_TO_NS(delay);

		enqueue(vde_conn, packet);
		setTimer(vde_conn);
	}
	else {
		sendPacket(vde_conn, packet);
		free(packet);
	}
}


/**
 * 
 * Time related functions
 * 
*/

/* Returns the current timestamp in microseconds */
static unsigned long long now_us() {
	struct timeval v;
	gettimeofday(&v,NULL);
	return (unsigned long long)(v.tv_sec*1000000000 + v.tv_usec*1000); 
}

/* Sets the timerfd for the next packet to send */
static void setTimer(struct vde_wirefilter_conn *vde_conn) {
	long long next_time_step = queue_head(vde_conn)->forward_time - now_us();
	if (next_time_step <= 0) next_time_step = 1;

	time_t seconds = next_time_step / (1000000000);
	long nseconds = next_time_step % (1000000000);
	struct itimerspec next = { { 0, 0 }, { seconds, nseconds } };
	
	timerfd_settime(vde_conn->queue_timer, 0, &next, NULL);
}



/**
 * 
 * Packet queue related functions
 * !! Currently with a temporary implementation !!
 * 
*/

static void enqueue(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
	int i=0;
	while (i < vde_conn->queue_size && vde_conn->queue[i]->forward_time <= packet->forward_time) { i++; }

	for (int j=vde_conn->queue_size-1; j>=i; j--) { vde_conn->queue[j+1] = vde_conn->queue[j]; }
	vde_conn->queue[i] = packet;
	vde_conn->queue_size++;
}

static Packet *dequeue(struct vde_wirefilter_conn *vde_conn) {
	if (vde_conn->queue_size == 0) { return NULL; }
	Packet *packet;

	packet = vde_conn->queue[0];
	
	for (int i=0; i<vde_conn->queue_size-1; i++) {
		vde_conn->queue[i] = vde_conn->queue[i+1];
	}
	vde_conn->queue_size--;

	return packet;
}

static Packet *queue_head(struct vde_wirefilter_conn *vde_conn) {
	if (vde_conn->queue_size == 0) { return NULL; }
	return vde_conn->queue[0];
}