#ifndef INCLUDE_MARKOV
#define INCLUDE_MARKOV


#define DELAY 0
#define DUP 1
#define LOSS 2
#define MTU 3
#define MARKOV_NODE_VALUES 4

#define MARKOV_GET_NODE(vde_conn, node) (vde_conn)->markov.nodes[(node)]
#define MARKOV_CURRENT(vde_conn) 		MARKOV_GET_NODE(vde_conn, (vde_conn)->markov.current_node)


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


void markov_init(struct vde_wirefilter_conn *vde_conn);
void markov_resize(struct vde_wirefilter_conn *vde_conn, int new_nodes_count);

void setWireValue(MarkovNode *node, int tag, char* bidirectional_value_str, char* lr_value_str, char *rl_value_str);
double maxWireValue(MarkovNode *node, int tag, int direction);
double minWireValue(MarkovNode *node, int tag, int direction);
double computeWireValue(MarkovNode *node, int tag, int direction);


#endif