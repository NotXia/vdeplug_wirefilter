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
#include <wf_management.h>
#include <wf_log.h>


#define DROP -1
#define FORWARD 0


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
static void sendPacket(struct vde_wirefilter_conn *vde_conn, Packet *packet);

static char mtuHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static char lossHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static int duplicatesHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static char bufferSizeHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static double bandwidthHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static double speedHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static double delayHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet);
static Packet *noiseHandler(struct vde_wirefilter_conn *vde_conn, Packet *packet);

static void openBlinkSocket(struct vde_wirefilter_conn *vde_conn, char *socket_path);
static int setBlinkId(struct vde_wirefilter_conn *vde_conn, char *id);
static void closeBlink(struct vde_wirefilter_conn *vde_conn);

static void savePidFile(char *path);


static VDECONN *vde_wirefilter_open(char *vde_url, char *descr, int interface_version, struct vde_open_args *open_args) {
	(void)descr; (void)interface_version; (void)open_args;

	init_logs();

	// Random seed
	struct timeval v;
	gettimeofday(&v,NULL);
	srand48(v.tv_sec ^ v.tv_usec ^ getpid());

	struct vde_wirefilter_conn *new_conn = NULL;
	VDECONN *nested_conn;
	char *nested_vnl;
	char *rc_path = NULL;
	char *delay_str = NULL;
	char *dup_str = NULL;
	char *loss_str = NULL;
	char *bursty_loss_str = NULL;
	char *mtu_str = NULL;
	char *nofifo_str = NULL;
	char *channel_size_str = NULL;
	char *bandwidth_str = NULL;
	char *speed_str = NULL;
	char *noise_str = NULL;
	char *blink_path_str = NULL, *blink_id_str = NULL;
	char *management_socket_path = NULL, *management_mode_str = NULL;
	char *pid_file_path = NULL;
	struct vdeparms parms[] = {
		{ "rc", &rc_path },
		{ "delay", &delay_str },
		{ "dup", &dup_str },
		{ "loss", &loss_str },
		{ "lostburst", &bursty_loss_str },
		{ "mtu", &mtu_str },
		{ "nofifo", &nofifo_str },
		{ "bufsize", &channel_size_str },
		{ "bandwidth", &bandwidth_str },
		{ "speed", &speed_str },
		{ "noise", &noise_str },
		{ "blink", &blink_path_str }, { "blinkid", &blink_id_str },
		{ "mgmt", &management_socket_path }, { "mgmtmode", &management_mode_str },
		{ "pidfile", &pid_file_path },
		{ NULL, NULL }
	};

	nested_vnl = vde_parsenestparms(vde_url);							// Gets the nested VNL
	if ( vde_parsepathparms(vde_url, parms) != 0 ) { return NULL; } 	// Retrieves the plugin parameters
	
	// Opens the connection with the nested VNL
	nested_conn = vde_open(nested_vnl, descr, open_args);
	if (nested_conn == NULL) { return NULL; }
	
	if ( (new_conn = calloc(1, sizeof(struct vde_wirefilter_conn))) == NULL ) {
		errno = ENOMEM;
		goto error;
	}
	new_conn->conn = nested_conn;

	// Pipes initialization
	new_conn->send_pipefd = malloc(2*sizeof(int));
	new_conn->receive_pipefd = malloc(2*sizeof(int));
	if ( pipe(new_conn->send_pipefd) != 0 ) { goto error; } 
	if ( pipe(new_conn->receive_pipefd) != 0 ) { goto error; } 

	initQueue(new_conn, nofifo_str == NULL ? FIFO : NO_FIFO);

	initMarkov(new_conn, 1, 0, MS_TO_NS(100));
	
	setWireValue(new_conn, DELAY, delay_str, 0);
	setWireValue(new_conn, DUP, dup_str, 0);
	setWireValue(new_conn, LOSS, loss_str, 0);
	setWireValue(new_conn, BURSTYLOSS, bursty_loss_str, 0);
	setWireValue(new_conn, MTU, mtu_str, WIRE_BIDIRECTIONAL);
	setWireValue(new_conn, CHANBUFSIZE, channel_size_str, WIRE_BIDIRECTIONAL);
	setWireValue(new_conn, BANDWIDTH, bandwidth_str, 0);
	setWireValue(new_conn, SPEED, speed_str, 0);
	setWireValue(new_conn, NOISE, noise_str, 0);
	new_conn->speed_timer = timerfd_create(CLOCK_REALTIME, 0);
	new_conn->bandwidth_next[LEFT_TO_RIGHT] = 0;
	new_conn->bandwidth_next[RIGHT_TO_LEFT] = 0;

	if (blink_path_str) { 
		openBlinkSocket(new_conn, blink_path_str); 
		if ( setBlinkId(new_conn, blink_id_str) == -1 ) { goto error; };
	}

	new_conn->management.socket_fd = -1;
	if (management_socket_path) {
		if ( initManagement(new_conn, management_socket_path, management_mode_str) < 0 ) { goto error; };
	}

	if (rc_path) {
		loadConfig(new_conn, -1, rc_path);
	}

	if (pid_file_path) {
		savePidFile(pid_file_path);
	}

	// Starts packet handler thread
	pthread_mutex_init(&new_conn->receive_lock, NULL);
	if ( pthread_create(&new_conn->packet_handler_thread, NULL, &packetHandlerThread, (void*)new_conn) != 0 ) { goto error; };

	return (VDECONN *)new_conn;

	error:
		vde_close(nested_conn);
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

	if ( poll(poll_fd, POLL_SIZE, -1) > 0 ) {
		// A packet arrived from the thread (which means that it can be received)
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
	uint64_t now = now_ns();

	// Speed delay handling
	if (vde_conn->speed_next[LEFT_TO_RIGHT] > now) {
		usleep( NS_TO_US(vde_conn->speed_next[LEFT_TO_RIGHT] - now) );
	}

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
	pthread_mutex_destroy(&vde_conn->receive_lock);

	close(vde_conn->send_pipefd[0]);
	close(vde_conn->send_pipefd[1]);
	close(vde_conn->receive_pipefd[0]);
	close(vde_conn->receive_pipefd[1]);
	free(vde_conn->send_pipefd);
	free(vde_conn->receive_pipefd);
	close(vde_conn->speed_timer);
	closeQueue(vde_conn);
	closeMarkov(vde_conn);
	if (vde_conn->management.socket_fd > 0) { closeManagement(vde_conn); }
	if (vde_conn->blink.socket_fd > 0) { closeBlink(vde_conn); }
	
	int ret_value = vde_close(vde_conn->conn); // Closes nested connection
	free(vde_conn);
	return ret_value;
}

#define POLL_PIPE_LR 		0
#define POLL_PIPE_RL 		1
#define POLL_QUEUE_TIMER 	2
#define POLL_SPEED_TIMER	3
#define POLL_MARKOV_TIMER	4
#define POLL_MNGM 			5

static void *packetHandlerThread(void *param) {
	pthread_detach(pthread_self());

	struct vde_wirefilter_conn *vde_conn = (struct vde_wirefilter_conn *)param;
	
	const int POLL_SIZE = 6+MNGM_MAX_CONN;
	struct pollfd poll_fd[6+MNGM_MAX_CONN] = {
		{ .fd=vde_conn->send_pipefd[0], .events=POLLIN },					// Left to right packets
		{ .fd=vde_datafd(vde_conn->conn), .events=POLLIN },					// Right to left packets
		{ .fd=vde_conn->queue.timerfd, .events=POLLIN },					// Packet queue timer
		{ .fd=vde_conn->speed_timer, .events=POLLIN },						// Packet speed timer
		{ .fd=vde_conn->markov.timerfd, .events=POLLIN },					// Markov chain state change
		{ .fd=vde_conn->management.socket_fd, .events=POLLIN },				// Management socket
	};
	for (int i=1; i<=MNGM_MAX_CONN; i++) { poll_fd[POLL_MNGM + i].fd = -1; } // Management socket clients

	ssize_t rw_len;
	unsigned char receive_buffer[VDE_ETHBUFSIZE];
	uint64_t now;


	// Starts Markov timer
	setTimer(vde_conn->markov.timerfd, vde_conn->markov.change_frequency);


	while(1) {
		if (poll(poll_fd, POLL_SIZE, -1) > 0) {

			// A packet has to be sent
			if (poll_fd[POLL_PIPE_LR].revents & POLLIN) {
				Packet *packet;
				rw_len = read(vde_conn->send_pipefd[0], &packet, sizeof(void*));
				if (__builtin_expect(rw_len < 0, 0)) { errno = EAGAIN; }

				handlePacket(vde_conn, packet);
				packetDestroy(packet);
			}


			// A packet can be received from the nested plugin
			if (poll_fd[POLL_PIPE_RL].revents & POLLIN) {
				now = now_ns();

				// Speed handling
				if (vde_conn->speed_next[RIGHT_TO_LEFT] > now) {
					poll_fd[POLL_PIPE_RL].events &= ~POLLIN; // Stop receiving packets
					setTimer(vde_conn->speed_timer, (vde_conn->speed_next[RIGHT_TO_LEFT] - now));
				}
				else {
					rw_len = vde_recv(vde_conn->conn, receive_buffer, VDE_ETHBUFSIZE, 0);
					
					if (rw_len > 1) { // Not discarded packet
						Packet *packet = malloc(sizeof(Packet));
						packet->buf = malloc(rw_len);
						memcpy(packet->buf, receive_buffer, rw_len);
						packet->len = rw_len;
						packet->flags = 0;
						packet->direction = RIGHT_TO_LEFT;

						handlePacket(vde_conn, packet);
						packetDestroy(packet);
					}
				}
			}


			// Time to send something
			if (poll_fd[POLL_QUEUE_TIMER].revents & POLLIN) {
				disarmTimer(vde_conn->queue.timerfd);

				Packet *packet;
				while (vde_conn->queue.size > 0 && nextQueueTime(vde_conn) < now_ns()) {
					packet = dequeue(vde_conn);
					sendPacket(vde_conn, packet);
				}

				// Sets the timer for the next packet
				if (vde_conn->queue.size > 0) {
					setQueueTimer(vde_conn);
				}
			}
			

			// Packets reception (right to left) can be restored (speed handling)
			if (poll_fd[POLL_SPEED_TIMER].revents & POLLIN) {
				disarmTimer(vde_conn->speed_timer);
				poll_fd[POLL_PIPE_RL].events |= POLLIN; // Restart receiving packets
			}


			// Time to change markov chain state
			if (poll_fd[POLL_MARKOV_TIMER].revents & POLLIN) {
				setTimer(vde_conn->markov.timerfd, vde_conn->markov.change_frequency);
				markovStep(vde_conn, vde_conn->markov.current_node);
			}


			// Management socket connection
			if (poll_fd[POLL_MNGM].revents & POLLIN) {
				int new_conn = acceptManagementConnection(vde_conn);

				if (new_conn > 0) {
					poll_fd[POLL_MNGM + vde_conn->management.connections_count].fd = new_conn;
					poll_fd[POLL_MNGM + vde_conn->management.connections_count].events = POLLIN | POLLHUP;
				}
			}

			// Management socket command
			for (int i=1; i<=vde_conn->management.connections_count; i++) {
				if (poll_fd[POLL_MNGM + i].revents & POLLIN) {
					handleManagementCommand(vde_conn, poll_fd[POLL_MNGM + i].fd);
				}
			}

			// Management socket hang-up
			for (int i=1; i<=vde_conn->management.connections_count; i++) {
				if (poll_fd[POLL_MNGM + i].revents & POLLHUP) {
					closeManagementConnection(vde_conn, poll_fd[POLL_MNGM + i].fd);

					// Shifts poll fds
					memmove(&poll_fd[POLL_MNGM+i], &poll_fd[POLL_MNGM+i+1], sizeof(struct pollfd) * (vde_conn->management.connections_count-i));
					poll_fd[POLL_MNGM+vde_conn->management.connections_count].fd = -1;

					vde_conn->management.connections_count--;
					i--;
				}
			}

		}
	}

	pthread_exit(0);
}


static void handlePacket(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	if (mtuHandler(vde_conn, packet) == DROP) { return; }
	if (lossHandler(vde_conn, packet) == DROP) { return; }

	double delay_ms = 0;
	int send_times = 1 + duplicatesHandler(vde_conn, packet);

	for (int i=0; i<send_times; i++) {
		Packet *to_send = packetCopy(packet);
		delay_ms = 0;

		if (bufferSizeHandler(vde_conn, to_send) == DROP) { return; }

		delay_ms += speedHandler(vde_conn, to_send);
		delay_ms += bandwidthHandler(vde_conn, to_send);
		delay_ms += delayHandler(vde_conn, to_send);

		noiseHandler(vde_conn, to_send);

		if (delay_ms > 0 || (vde_conn->queue.fifoness == FIFO && vde_conn->queue.size > 0)) {
			enqueue(vde_conn, to_send, now_ns() + MS_TO_NS(delay_ms));
			setQueueTimer(vde_conn);
		}
		else {
			sendPacket(vde_conn, to_send);
		}
	}
}


/* Sends the packet to the correct destination */
static void sendPacket(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
	// Blink message handling
	if (vde_conn->blink.socket_fd) {
		char *message_content = vde_conn->blink.message + (vde_conn->blink.id_len+1); // Skip id and blank
		
		snprintf(message_content, BLINK_MESSAGE_CONTENT_SIZE, "%s %ld\n",
				(packet->direction == LEFT_TO_RIGHT) ? "LR" : ((packet->direction == RIGHT_TO_LEFT) ? "RL" : "--"), packet->len);		
		sendto(vde_conn->blink.socket_fd, vde_conn->blink.message, strlen(vde_conn->blink.message), 0, 
				(struct sockaddr *)&vde_conn->blink.socket_info, sizeof(vde_conn->blink.socket_info));
	}

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

	free(packet);
}


static char mtuHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	if (minWireValue(MARKOV_CURRENT(vde_conn), MTU, packet->direction) > 0 && 
		packet->len > minWireValue(MARKOV_CURRENT(vde_conn), MTU, packet->direction)) {
		return DROP;
	}

	return FORWARD;
}

static char lossHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	// Total loss
	if ( minWireValue(MARKOV_CURRENT(vde_conn), LOSS, packet->direction) >= 100.0 ) { return DROP; }

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

		if (vde_conn->bursty_loss_status[packet->direction] != OK_BURST) { return DROP; }
	}
	else {
		vde_conn->bursty_loss_status[packet->direction] = OK_BURST;
		
		// Standard loss handling
		if (drand48() < (computeWireValue(MARKOV_CURRENT(vde_conn), LOSS, packet->direction) / 100)) {
			return DROP;
		}
	}

	return FORWARD;
}

static int duplicatesHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	int duplicate_times = 0;

	if (maxWireValue(MARKOV_CURRENT(vde_conn), DUP, packet->direction) > 0) {
		while (drand48() < (computeWireValue(MARKOV_CURRENT(vde_conn), DUP, packet->direction) / 100)) { 
			duplicate_times++; 
		}
	}

	return duplicate_times;
}

static char bufferSizeHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	if (maxWireValue(MARKOV_CURRENT(vde_conn), CHANBUFSIZE, packet->direction)) {
		double buffer_max_size = computeWireValue(MARKOV_CURRENT(vde_conn), CHANBUFSIZE, packet->direction);
		
		if ((vde_conn->queue.byte_size[packet->direction] + packet->len) > buffer_max_size) {
			return DROP;
		}
	}

	return FORWARD;
}

static double bandwidthHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	double delay_ms = 0;

	if (maxWireValue(MARKOV_CURRENT(vde_conn), BANDWIDTH, packet->direction) > 0) {
		double bandwidth = computeWireValue(MARKOV_CURRENT(vde_conn), BANDWIDTH, packet->direction);
		if (bandwidth <= 0) { return DROP; }

		double send_time_ms = (packet->len*1000) / bandwidth;
		uint64_t now = now_ns();

		if (now > vde_conn->bandwidth_next[packet->direction]) {
			// Bandwidth is still below the limit, delay this one to keep the bandwidth up to the limit
			vde_conn->bandwidth_next[packet->direction] = now;
			delay_ms = send_time_ms;
		} else {
			// Bandwidth is overflowing, delay this one until the next bandwidth timestamp 
			double diff = NS_TO_MS( vde_conn->bandwidth_next[packet->direction] - now );
			delay_ms = diff + send_time_ms;
		}
		vde_conn->bandwidth_next[packet->direction] += MS_TO_NS(send_time_ms);
	}

	return delay_ms;
}

static double speedHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	double delay_ms = 0;

	if (maxWireValue(MARKOV_CURRENT(vde_conn), SPEED, packet->direction) > 0) {
		double speed = computeWireValue(MARKOV_CURRENT(vde_conn), SPEED, packet->direction);
		if (speed <= 0) { return DROP; };

		double send_time_ms = (packet->len*1000) / speed;
		uint64_t now = now_ns();

		delay_ms = send_time_ms;
		if (now > vde_conn->speed_next[packet->direction]) {
			vde_conn->speed_next[packet->direction] = now;
		}
		vde_conn->speed_next[packet->direction] += MS_TO_NS(send_time_ms);
	}

	return delay_ms;
}

static double delayHandler(struct vde_wirefilter_conn *vde_conn, const Packet *packet) {
	double delay_ms = 0;

	if (maxWireValue(MARKOV_CURRENT(vde_conn), DELAY, packet->direction) > 0) {
		double delay_value = computeWireValue(MARKOV_CURRENT(vde_conn), DELAY, packet->direction);

		if (delay_value > 0) {
			delay_ms = delay_value;
		}
	}

	return delay_ms;
}

static Packet *noiseHandler(struct vde_wirefilter_conn *vde_conn, Packet *packet) {
	if (maxWireValue(MARKOV_CURRENT(vde_conn), NOISE, packet->direction) > 0) {
		double noise = computeWireValue(MARKOV_CURRENT(vde_conn), NOISE, packet->direction);
		int broken_bits = 0;
		
		// Determines the number of broken bits
		while ((drand48()*8*MEGA) < (packet->len-2)*8*noise) { broken_bits++; }
		
		// Breaks the packet
		for (int i=0; i<broken_bits; i++) {
			int to_flip_bit = drand48() * packet->len*8;
			((char*)packet->buf)[(to_flip_bit >> 3) + 2] ^= 1<<(to_flip_bit & 0x7);
		}
	} 

	return packet;
}


static void openBlinkSocket(struct vde_wirefilter_conn *vde_conn, char *socket_path) {
	vde_conn->blink.socket_info.sun_family = PF_UNIX;
	strncpy(vde_conn->blink.socket_info.sun_path, socket_path, sizeof(vde_conn->blink.socket_info.sun_path)-1);

	vde_conn->blink.socket_fd = socket(PF_UNIX, SOCK_DGRAM, 0);
}

static int setBlinkId(struct vde_wirefilter_conn *vde_conn, char *id) {
	char pid_str[6+1];
	char *to_set_id = id;

	// If id is not set, defaults to pid
	if (to_set_id == NULL) {
		snprintf(pid_str, sizeof(pid_str), "%06d", getpid());
		to_set_id = pid_str;
	}

	vde_conn->blink.id_len = strlen(to_set_id);
	return asprintf(&vde_conn->blink.message, "%s %*c", to_set_id, BLINK_MESSAGE_CONTENT_SIZE, ' ');
}

static void closeBlink(struct vde_wirefilter_conn *vde_conn) {
	close(vde_conn->blink.socket_fd);
	remove(vde_conn->blink.socket_info.sun_path);
}


static void savePidFile(char *path) {
	FILE *fout = NULL;

	if ( (fout = fopen(path, "w")) == NULL ) { 
		print_log(LOG_ERR, "Error in FILE* construction: %s", strerror(errno));
		exit(1);
	}
	if ( fprintf(fout, "%d\n", getpid()) < 0 ) { 
		print_log(LOG_ERR, "Error in writing pidfile"); 
		exit(1);
	}

	fclose(fout);
}