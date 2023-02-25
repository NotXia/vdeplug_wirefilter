#ifndef INCLUDE_CONN
#define INCLUDE_CONN
#include <pthread.h>
#include <libvdeplug.h>
#include <libvdeplug_mod.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "./wf_markov.h"
#include "./wf_queue.h"

#define LEFT_TO_RIGHT 0
#define RIGHT_TO_LEFT 1
#define BIDIRECTIONAL 2

#define OK_BURST 0
#define FAULTY_BURST 1

#define NO_FIFO 0
#define FIFO	1

#define BLINK_MESSAGE_CONTENT_SIZE 20 // Size of blink messages without the id


struct packet_t {
	void *buf;
	size_t len;
	int flags;
	int direction;
};
typedef struct packet_t Packet;


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
	char fifoness;

	struct {
		QueueNode **queue; // Priority queue implemented as heap
		unsigned int size;
		unsigned int max_size;
		unsigned int byte_size[2];
		
		// To preserve fifoness
		unsigned long long max_forward_time;
		unsigned int counter;
	} queue;

	struct {
		MarkovNode **nodes;
		int current_node;
		int nodes_count;
		double *adjacency;
	} markov;

	struct {
		int socket_fd;
		struct sockaddr_un socket_info;
		char *message;
		char id_len;
	} blink;

	char bursty_loss_status[2];
};

Packet *packetCopy(const Packet *to_copy);
void packetDestroy(Packet *to_destroy);

#endif