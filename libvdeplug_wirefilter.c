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

// #define RESET_FLAGS 0
// #define CAN_RECEIVE 0x1

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1

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
	
	double duplication_probability_lr;
	double duplication_probability_rl;

	pthread_t packet_handler_thread;
	int *pipefd;
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

struct packetHandlerData {
	struct vde_wirefilter_conn *vde_conn;
	int direction;
	void *buf;
	size_t len;
	int flags;
};

void *packetHandler(void *param);
int computeDuplicationCount(const double duplication_probability);


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void) descr;
	(void) interface_version;
	(void) open_args;

	struct vde_wirefilter_conn *newconn = NULL;
	VDECONN *conn;
	char *nested_vnl;
	char *duplication_probability_str 	 = NULL;
	char *duplication_probability_lr_str = "0.0";
	char *duplication_probability_rl_str = "0.0";
	struct vdeparms parms[] = {
		{ "dup", &duplication_probability_str },
		{ "dupLR", &duplication_probability_lr_str },
		{ "dupRL", &duplication_probability_rl_str },
		{ NULL, NULL }
	};

	nested_vnl = vde_parsenestparms(vde_url);						// Gets the nested VNL
	if (vde_parsepathparms(vde_url, parms) != 0) { return NULL; } 	// Retrieves the plugin parameters
	
	// Opens a connection with the nested VNL
	conn = vde_open(nested_vnl, descr, open_args);
	if (conn == NULL) { return  NULL; }
	if ((newconn=calloc(1, sizeof(struct vde_wirefilter_conn))) == NULL) {
		errno = ENOMEM;
		goto error;
	}

	int pipefd[2];
	pipe(pipefd);

	// Starts packet handler thread
	pthread_t packet_handler_thread;
	if ( pthread_create(&packet_handler_thread, NULL, &packetHandler, (void*)&pipefd[0]) != 0 ) { 
		goto error;
	};

	newconn->conn=conn;
	newconn->duplication_probability_lr = duplication_probability_str != NULL ? atof(duplication_probability_str) : atof(duplication_probability_lr_str);
	newconn->duplication_probability_rl = duplication_probability_str != NULL ? atof(duplication_probability_str) : atof(duplication_probability_rl_str);
	newconn->packet_handler_thread = packet_handler_thread;
	newconn->pipefd = pipefd;

	return (VDECONN *)newconn;

error:
	vde_close(conn);
	return NULL;
}

// Right to left
static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	if (1) {
		return vde_recv(vde_conn->conn, buf, len, flags);
	}

	struct packetHandlerData *tmp = malloc(sizeof(struct packetHandlerData));
	tmp->vde_conn = vde_conn;
	tmp->direction = RIGHT_TO_LEFT;
	tmp->buf = malloc(len);
	memcpy(tmp->buf, buf, len);
	tmp->len = len;
	tmp->flags = flags;

	// Passes the packet to the handler
	write(vde_conn->pipefd[1], &tmp, sizeof(tmp));

	return 1;


	// if (drand48() < vde_conn->duplication_probability_rl/100) {
	// 	vde_send(conn, buf, len, flags); // Resends the same packet to myself
	// }
	// return vde_recv(vde_conn->conn, buf, len, flags);


	/* --- Attempt with flags --- */
	// if (flags & CAN_RECEIVE) {
	// 	return vde_recv(vde_conn->conn, buf, len, RESET_FLAGS);
	// }

	// // Duplication handling
	// int to_send_packets = 1;
	// to_send_packets += computeDuplicationCount(vde_conn->duplication_probability_rl);
	// for (int i=0; i<to_send_packets; i++) {
	// 	vde_send(conn, buf, len, flags | CAN_RECEIVE); // Resends the same packet to myself
	// }

	// return 1; // Drops the current packet
}

// Left to right
static ssize_t vde_wirefilter_send(VDECONN *conn, const void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	struct packetHandlerData *tmp = malloc(sizeof(struct packetHandlerData));
	tmp->vde_conn = vde_conn;
	tmp->direction = LEFT_TO_RIGHT;
	tmp->buf = malloc(len);
	memcpy(tmp->buf, buf, len);
	tmp->len = len;
	tmp->flags = flags;

	// Passes the packet to the handler
	write(vde_conn->pipefd[1], &tmp, sizeof(tmp));

	return 0;
}

static int vde_wirefilter_datafd(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_datafd(vde_conn->conn);
}

static int vde_wirefilter_ctlfd(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_ctlfd(vde_conn->conn);
}

static int vde_wirefilter_close(VDECONN *conn) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	pthread_cancel(vde_conn->packet_handler_thread);
	close(vde_conn->pipefd[0]);
	close(vde_conn->pipefd[1]);
	
	int ret_value = vde_close(vde_conn->conn);
	free(vde_conn);
	return ret_value;
}



void *packetHandler(void *param) {
	pthread_detach(pthread_self());

	int receive_fd = *((int *)param);
	int poll_ret;
	struct pollfd poll_fd[1] = {
		{ .fd=receive_fd, .events=POLLIN }
	};

	while(1) {
		poll_ret = poll(poll_fd, 1, -1);

		if (poll_ret > 0) {
			if (poll_fd[0].revents & POLLIN) {
				struct packetHandlerData *tmp;
				read(receive_fd, &tmp, sizeof(tmp));

				if (tmp->direction == RIGHT_TO_LEFT) { // Packet forwarded to myself
					vde_send((VDECONN *)tmp->vde_conn, tmp->buf, tmp->len, tmp->flags);
				}
				else {
					vde_send(tmp->vde_conn->conn, tmp->buf, tmp->len, tmp->flags);
				}

				free(tmp);
			}
		}
		else {
			/* Error */
		}
	}

	pthread_exit(0);
}



/* Returns the number of times a packet should be duplicated */
int computeDuplicationCount(const double duplication_probability) {
	int times = 0;

	while (drand48() < (duplication_probability/100)) {
		times++;
	}

	return times;
}
