#ifndef INCLUDE_MANAGEMENT
#define INCLUDE_MANAGEMENT

#include "./wf_conn.h"

#define MNGM_CMD_MAX_LEN 128

int initManagement(struct vde_wirefilter_conn *vde_conn, char *socket_path, char *mode_str);
void closeManagement(struct vde_wirefilter_conn *vde_conn);

int createManagementSocket(struct vde_wirefilter_conn *vde_conn, char *socket_name);
int acceptManagementConnection(struct vde_wirefilter_conn *vde_conn);
int closeManagementConnection(struct vde_wirefilter_conn *vde_conn, int socket_fd);

int handleManagementCommand(struct vde_wirefilter_conn *vde_conn, int socket_fd);
int loadConfig(struct vde_wirefilter_conn *vde_conn, int fd, char *rc_path);

int print_mgmt(int fd, const char *format, ...);

#endif