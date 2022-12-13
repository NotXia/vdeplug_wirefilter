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
	void *buf;
	size_t len;
	int flags;
};

static void *packetHandler(void *param);


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void)descr; (void)interface_version; (void)open_args;

	struct vde_wirefilter_conn *newconn = NULL;
	VDECONN *conn;
	char *nested_vnl;
	struct vdeparms parms[] = {
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
	if ( pthread_create(&packet_handler_thread, NULL, &packetHandler, (void*)newconn) != 0 ) { goto error; };
	newconn->packet_handler_thread = packet_handler_thread;

	pthread_mutex_init(&newconn->receive_lock, NULL);

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

	struct packetHandlerData *packet = malloc(sizeof(struct packetHandlerData));
	if (__builtin_expect(packet == NULL, 0)) { goto error; }
	packet->buf = malloc(len);
	if (__builtin_expect(packet->buf == NULL, 0)) { goto error; }

	memcpy(packet->buf, buf, len);
	packet->len = len;
	packet->flags = flags;

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


static void *packetHandler(void *param) {
	pthread_detach(pthread_self());

	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)param;
	
	const int POLL_SIZE = 2;
	struct pollfd poll_fd[2] = {
		{ .fd=vde_conn->send_pipefd[0], .events=POLLIN },
		{ .fd=vde_datafd(vde_conn->conn), .events=POLLIN }
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
				struct packetHandlerData *packet;
				rw_len = read(vde_conn->send_pipefd[0], &packet, sizeof(void*));
				if (__builtin_expect(rw_len < 0, 0)) { errno = EAGAIN; }

				vde_send(vde_conn->conn, packet->buf, packet->len, packet->flags);

				free(packet);
			}

			// A packet can be received from the nested plugin
			if (poll_fd[1].revents & POLLIN) {
				receive_length = vde_recv(vde_conn->conn, receive_buffer, VDE_ETHBUFSIZE, 0);
				if (receive_length == 1) { continue; } // Discarded

				pthread_mutex_lock(&vde_conn->receive_lock);
				// Makes the packet receivable
				rw_len = write(vde_conn->receive_pipefd[1], receive_buffer, receive_length);
				if (__builtin_expect(rw_len < 0, 0)) { errno = EAGAIN; }
			}
		}
	}

	pthread_exit(0);
}