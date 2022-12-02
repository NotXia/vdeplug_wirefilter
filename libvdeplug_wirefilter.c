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


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void) descr;
	(void) interface_version;
	(void) open_args;

	struct vde_wirefilter_conn *newconn = NULL;
	char *nested_vnl;
	struct vdeparms parms[] = {
		{NULL, NULL}
	};
	VDECONN *conn;

	nested_vnl = vde_parsenestparms(vde_url);						// Gets the nested VNL
	if (vde_parsepathparms(vde_url, parms) != 0) { return NULL; } 	// Retrieves the plugin parameters
	
	// Open a connection with the nested VNL
	conn = vde_open(nested_vnl, descr, open_args);
	if (conn == NULL) { return  NULL; }
	if ((newconn=calloc(1, sizeof(struct vde_wirefilter_conn))) == NULL) {
		errno = ENOMEM;
		goto error;
	}
	newconn->conn=conn;

	return (VDECONN *)newconn;

error:
	vde_close(conn);
	return NULL;
}

static ssize_t vde_wirefilter_recv(VDECONN *conn, void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_recv(vde_conn->conn, buf, len, flags);
}

static ssize_t vde_wirefilter_send(VDECONN *conn, const void *buf, size_t len, int flags) {
	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)conn;
	return vde_send(vde_conn->conn, buf, len, flags);
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
