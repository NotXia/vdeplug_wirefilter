#ifndef INCLUDE_CONN
#define INCLUDE_CONN
#include <pthread.h>
#include <libvdeplug.h>
#include <libvdeplug_mod.h>


typedef struct {
	void *buf;
	size_t len;
	int flags;
	int direction;

	unsigned long long forward_time; 
} Packet;


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

#endif