#ifndef INCLUDE_MARKOV
#define INCLUDE_MARKOV

#include <stdint.h>


#define DELAY 0
#define DUP 1
#define LOSS 2
#define BURSTYLOSS 3
#define MTU 4
#define CHANBUFSIZE 5
#define BANDWIDTH 6
#define SPEED 7
#define NOISE 8
#define MARKOV_NODE_VALUES 9

#define MARKOV_GET_NODE(vde_conn, node) (vde_conn)->markov.nodes[(node)]
#define MARKOV_CURRENT(vde_conn) 		MARKOV_GET_NODE(vde_conn, (vde_conn)->markov.current_node)

#define WIRE_BIDIRECTIONAL 		0x1

#define ALGO_UNIFORM      0
#define ALGO_GAUSS_NORMAL 1
#define SIGMA (1.0/3.0) // more than 98% inside the bell

#define KILO (1<<10)
#define MEGA (1<<20)
#define GIGA (1<<30)


typedef struct {
	double value;
	double plus;
	char algorithm;
} WireValue;

typedef struct {
    char *name;
	WireValue value[MARKOV_NODE_VALUES][2];
} MarkovNode;

struct vde_wirefilter_conn;


void markov_init(struct vde_wirefilter_conn *vde_conn, int size, uint64_t change_frequency);
void markov_setEdges(struct vde_wirefilter_conn *vde_conn, char *edges_str);
void markov_resize(struct vde_wirefilter_conn *vde_conn, int new_nodes_count);
void markov_step(struct vde_wirefilter_conn *vde_conn, const int start_node);

void setWireValue(struct vde_wirefilter_conn *vde_conn, int tag, char *value_str, int flags);
double maxWireValue(MarkovNode *node, int tag, int direction);
double minWireValue(MarkovNode *node, int tag, int direction);
double computeWireValue(MarkovNode *node, int tag, int direction);


#endif