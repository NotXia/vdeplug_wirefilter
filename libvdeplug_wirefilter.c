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

#define RESET_FLAGS 0
#define CAN_RECEIVE 0x1

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

	newconn->conn=conn;
	newconn->duplication_probability_lr = duplication_probability_str != NULL ? atof(duplication_probability_str) : atof(duplication_probability_lr_str);
	newconn->duplication_probability_rl = duplication_probability_str != NULL ? atof(duplication_probability_str) : atof(duplication_probability_rl_str);
	
	return (VDECONN *)newconn;

error:
	vde_close(conn);
	return NULL;
}

// Right to left
static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;

	if (drand48() < vde_conn->duplication_probability_rl/100) {
		vde_send(conn, buf, len, flags | CAN_RECEIVE); // Resends the same packet to myself
	}
	return vde_recv(vde_conn->conn, buf, len, RESET_FLAGS);

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

	// Duplication handling
	int to_send_packets = 1;
	to_send_packets += computeDuplicationCount(vde_conn->duplication_probability_lr);
	for (int i=0; i<to_send_packets; i++) {
		vde_send(vde_conn->conn, buf, len, flags);
	}

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
	int ret_value = vde_close(vde_conn->conn);
	free(vde_conn);
	return ret_value;
}


/* Returns the number of times a packet should be duplicated */
int computeDuplicationCount(const double duplication_probability) {
	int times = 0;

	while (drand48() < (duplication_probability/100)) {
		times++;
	}

	return times;
}
