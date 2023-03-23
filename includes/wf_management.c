#include "./wf_management.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "./wf_markov.h"
#include "./wf_time.h"
#include "./wf_debug.h"


#define PACKAGE_VERSION "1.0"

static char header[]="\nVDE wirefilter V.%s\n";
static char prompt[]="\nVDEwf$ ";

#define WITHFILE 0x80


int createManagementSocket(struct vde_wirefilter_conn *vde_conn, char *socket_name) {
	int socket_fd;
	struct sockaddr_un sun;
	int one = 1;

	if ( (socket_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0 ) { return -1; }
	if ( setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one) ) < 0) { return -1; }
	if ( fcntl(socket_fd, F_SETFL, O_NONBLOCK) < 0 ) { return -1; }
	sun.sun_family = AF_UNIX;
	snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", socket_name);

	if ( bind(socket_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0 ) { return -1; }
	chmod(sun.sun_path, vde_conn->management.mode);
	if ( listen(socket_fd, 15) < 0 ) { return -1; }
	
    vde_conn->management.socket_fd = socket_fd;

    return 0;
}


int acceptManagementConnection(struct vde_wirefilter_conn *vde_conn) {
	int new_connection;
	char buf[MNGM_CMD_MAX_LEN];

	new_connection = accept(vde_conn->management.socket_fd, NULL, NULL);
	if (new_connection < 0) { return -1; }
	
	if (vde_conn->management.connections_count < MNGM_MAX_CONN) {
		snprintf(buf, MNGM_CMD_MAX_LEN, header, PACKAGE_VERSION);
		if ( write(new_connection, buf, strlen(buf)) < 0 ) { return -1; }
		if ( write(new_connection, prompt, strlen(prompt)) < 0 ) { return -1; }

        vde_conn->management.connections_count++;
		return new_connection;
	} else {
		close(new_connection);
		return -1;
	}
}


static int print_mgmt(int fd, const char *format, ...) {
	va_list arg;
	char out[MNGM_CMD_MAX_LEN + 1];

	va_start(arg, format);
	vsnprintf(out, MNGM_CMD_MAX_LEN, format, arg);
	strcat(out, "\n");
	return write(fd, out, strlen(out));
}


static int help(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)vde_conn; (void)arg;
	print_mgmt(fd, "COMMAND      HELP");
	print_mgmt(fd, "------------ ------------");
	print_mgmt(fd, "help         print a summary of mgmt commands");
	// print_mgmt(fd, "load         load a configuration file");
	// print_mgmt(fd, "showinfo     show status and parameter values");
	print_mgmt(fd, "loss         set loss percentage");
	print_mgmt(fd, "lostburst    mean length of lost packet bursts");
	print_mgmt(fd, "delay        set delay ms");
	print_mgmt(fd, "dup          set dup packet percentage");
	print_mgmt(fd, "bandwidth    set channel bandwidth bytes/sec");
	print_mgmt(fd, "speed        set interface speed bytes/sec");
	print_mgmt(fd, "noise        set noise factor bits/Mbyte");
	print_mgmt(fd, "mtu          set channel MTU (bytes)");
	print_mgmt(fd, "chanbufsize  set channel buffer size (bytes)");
	print_mgmt(fd, "fifo         set channel fifoness");
	// print_mgmt(fd, "shutdown     shut the channel down");
	// print_mgmt(fd, "logout       log out from this mgmt session");
	print_mgmt(fd, "markov-numnodes n  markov mode: set number of states");
	print_mgmt(fd, "markov-setnode n   markov mode: set current state");
	print_mgmt(fd, "markov-name n,name markov mode: set state's name");
	print_mgmt(fd, "markov-time ms     markov mode: transition period");
	print_mgmt(fd, "setedge n1,n2,w    markov mode: set edge weight");
	// print_mgmt(fd, "showinfo n         markov mode: show parameter values");
	print_mgmt(fd, "showedges n        markov mode: show edge weights");
	print_mgmt(fd, "showcurrent        markov mode: show current state");
	// print_mgmt(fd, "markov-debug n     markov mode: set debug level");
	return 0;
}

static int setLoss(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, LOSS, arg, 0);
	return 0;
}

static int setBurstyLoss(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, BURSTYLOSS, arg, 0);
	return 0;
}

static int setDelay(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, DELAY, arg, 0);
	return 0;
}

static int setDuplicates(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, DUP, arg, 0);
	return 0;
}

static int setBandwidth(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, BANDWIDTH, arg, 0);
	return 0;
}

static int setSpeed(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, SPEED, arg, 0);
	return 0;
}

static int setNoise(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, NOISE, arg, 0);
	return 0;
}

static int setMTU(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, MTU, arg, WIRE_BIDIRECTIONAL);
	return 0;
}

static int setChanbufsize(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	setWireValue(vde_conn, CHANBUFSIZE, arg, WIRE_BIDIRECTIONAL);
	return 0;
}

static int setFIFO(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	vde_conn->fifoness = atoi(arg);
	return 0;
}

static int markovSetNodeNumber(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	markov_resize(vde_conn, atoi(arg));
	return 0;
}

static int markovSetCurrentNode(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	vde_conn->markov.current_node = atoi(arg);
	return 0;
}

static int markovSetNodeName(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	markov_setNames(vde_conn, arg);
	return 0;
}

static int markovSetTime(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	WF_DEBUG_PRINT(DEBUG_LOGS, "|%s| %lld %lld\n", arg, atoll(arg), MS_TO_NS(atoll(arg)));
	vde_conn->markov.change_frequency = MS_TO_NS(atoll(arg));
	setTimer(vde_conn->markov.timerfd, vde_conn->markov.change_frequency);
	return 0;
}

static int markovSetEdge(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)fd;
	markov_setEdges(vde_conn, arg);
	return 0;
}

static int markovShowEdges(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	int to_explore_node = (*arg != 0) ? atoi(arg) : vde_conn->markov.current_node;
	
	if (to_explore_node < 0 || to_explore_node >= vde_conn->markov.nodes_count) {
		return EINVAL;
	}

	for (int i=0; i<vde_conn->markov.nodes_count; i++) {
		if (ADJMAP(vde_conn, to_explore_node, i) != 0) {
			print_mgmt(
				fd, "Edge (%-2d)->(%-2d) \"%s\"->\"%s\" weight %lg",
				to_explore_node, i,
				MARKOV_GET_NODE(vde_conn, to_explore_node)->name ? MARKOV_GET_NODE(vde_conn, to_explore_node)->name : "",
				MARKOV_GET_NODE(vde_conn, i)->name ? MARKOV_GET_NODE(vde_conn, i)->name : "",
				ADJMAP(vde_conn, to_explore_node, i)
			);
		}
	} 

	return 0;
}

static int markovShowCurrent(struct vde_wirefilter_conn *vde_conn, int fd, char *arg) {
	(void)arg;
	print_mgmt(
		fd, "Current Markov Node %d \"%s\" (0,..,%d)", 
		vde_conn->markov.current_node, 
		MARKOV_CURRENT(vde_conn)->name ? MARKOV_CURRENT(vde_conn)->name : "",
		vde_conn->markov.nodes_count-1
	);
	return 0;
}


static struct comlist {
	char *tag;
	int (*fun)(struct vde_wirefilter_conn *vde_conn, int fd, char *arg);
	unsigned char type;
} commandlist [] = {
	{ "help", 			help, 			WITHFILE },
	{ "loss", 			setLoss, 		0 },
	{ "lostburst", 		setBurstyLoss,	0 },
	{ "delay", 			setDelay, 		0 },
	{ "dup", 			setDuplicates, 	0 },
	{ "bandwidth", 		setBandwidth, 	0 },
	{ "speed", 			setSpeed, 		0 },
	{ "noise", 			setNoise, 		0 },
	{ "mtu", 			setMTU, 		0 },
	{ "chanbufsize", 	setChanbufsize,	0 },
	{ "fifo", 			setFIFO,		0 },
	{ "markov-numnodes", markovSetNodeNumber, 0 },
	{ "markov-setnode", markovSetCurrentNode, 0 },
	{ "markov-name", markovSetNodeName, 0 },
	{ "markov-time", markovSetTime, 0 },
	{ "setedge", markovSetEdge, 0 },
	{ "showedges", markovShowEdges, WITHFILE },
	{ "showcurrent", markovShowCurrent, WITHFILE },
};
#define NUM_COMMANDS ( (int)(sizeof(commandlist)/sizeof(struct comlist)) )


static int findCommandIndex(char *cmd) {
	for (int i=0; i<NUM_COMMANDS; i++) {
		if (strncmp(commandlist[i].tag, cmd, strlen(commandlist[i].tag)) == 0) {
			return i;
		}
	}
	return -1;
}


static int executeCommand(struct vde_wirefilter_conn *vde_conn, int socket_fd, char *cmd) {
	int ret_value = ENOSYS;
	int command_index;
	// char *cmd_start = cmd;
	
	// Trim
    while (*cmd == ' ' || *cmd == '\t' || *cmd == '\n') { cmd++; }
	int len = strlen(cmd) - 1;
	while (len>0 && (cmd[len]=='\n' || cmd[len]==' ' || cmd[len]=='\t')) { cmd[len--] = '\0'; }

	if (*cmd != '\0' && *cmd != '#') {
		command_index = findCommandIndex(cmd);

		if (command_index >= 0) {
			// Moves the string to the argument
			cmd += strlen(commandlist[command_index].tag);
			while (*cmd == ' ' || *cmd == '\t') { cmd++; }

			// Command execution
			if (socket_fd >= 0 && commandlist[command_index].type & WITHFILE) { print_mgmt(socket_fd, "0000 DATA END WITH '.'"); }
			ret_value = commandlist[command_index].fun(vde_conn, socket_fd, cmd);
			if (socket_fd >= 0 && commandlist[command_index].type & WITHFILE) { print_mgmt(socket_fd, "."); }
		}

		if (socket_fd >= 0) {
			if (ret_value == 0) {
				print_mgmt(socket_fd, "1000 Success");
			} else {
				print_mgmt(socket_fd, "1%03d %s", ret_value, strerror(ret_value));
			}
		} else if (ret_value != 0) {
			// printlog(LOG_ERR,"rc command error: %s %s",cmd_start,strerror(ret_value));
		}
	}

	return ret_value;
}

int handleManagementCommand(struct vde_wirefilter_conn *vde_conn, int socket_fd) {
    char buf[MNGM_CMD_MAX_LEN+1];
    int n;

	n = read(socket_fd, buf, MNGM_CMD_MAX_LEN);

	if (n <= 0) {
		return -1;
	}
	else {
		buf[n] = '\0';
		int ret_value = executeCommand(vde_conn, socket_fd, buf);
		
		if (ret_value >= 0) { 
			if (write(socket_fd, prompt, strlen(prompt)) < 0) { return -1; } 
		}

		return ret_value;
	}
}