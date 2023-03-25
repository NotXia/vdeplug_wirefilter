#ifndef INCLUDE_MANAGEMENT
#define INCLUDE_MANAGEMENT

#include "./wf_conn.h"

#define MNGM_MAX_CONN 3
#define MNGM_CMD_MAX_LEN 128

int createManagementSocket(struct vde_wirefilter_conn *vde_conn, char *socket_name);
int acceptManagementConnection(struct vde_wirefilter_conn *vde_conn);
int handleManagementCommand(struct vde_wirefilter_conn *vde_conn, int socket_fd);
int loadConfig(struct vde_wirefilter_conn *vde_conn, int fd, char *rc_path);

#endif