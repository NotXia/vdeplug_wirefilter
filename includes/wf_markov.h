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

#define ADJMAPN(M, I, J, N) (M)[(I)*(N)+(J)]
#define ADJMAP(vde_conn, I, J) ADJMAPN((vde_conn)->markov.adjacency, (I), (J), (vde_conn)->markov.nodes_count)
#define MARKOV_GET_NODE(vde_conn, node) (vde_conn)->markov.nodes[(node)]
#define MARKOV_CURRENT(vde_conn) 		MARKOV_GET_NODE(vde_conn, (vde_conn)->markov.current_node)

#define WIRE_BIDIRECTIONAL 		0x1

#define ALGO_UNIFORM      0
#define ALGO_GAUSS_NORMAL 1
#define SIGMA (1.0/3.0) // more than 98% inside the bell

#define KILO (1<<10)
#define MEGA (1<<20)
#define GIGA (1<<30)

#define WIRE_FIELDS(node, tag, direction)	(node->value[tag][direction].value), (node->value[tag][direction].plus), (node->value[tag][direction].algorithm == ALGO_UNIFORM ? 'U' : 'N')


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


int initMarkov(struct vde_wirefilter_conn *vde_conn, const int size, const int start_node, const uint64_t change_frequency);
void closeMarkov(struct vde_wirefilter_conn *vde_conn);

void markovSetEdges(struct vde_wirefilter_conn *vde_conn, char *edges_str);
void markovSetNames(struct vde_wirefilter_conn *vde_conn, char *names_str);
int markovResize(struct vde_wirefilter_conn *vde_conn, const int new_nodes_count);
void markovStep(struct vde_wirefilter_conn *vde_conn, const int start_node);

void setWireValue(struct vde_wirefilter_conn *vde_conn, const int tag, char *value_str, const int flags);
double maxWireValue(MarkovNode *node, const int tag, const int direction);
double minWireValue(MarkovNode *node, const int tag, const int direction);
double computeWireValue(MarkovNode *node, const int tag, const int direction);


#endif