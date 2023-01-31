#ifndef INCLUDE_MARKOV
#define INCLUDE_MARKOV


#define DELAY 0
#define MARKOV_NODE_VALUES 1


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

void setWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction, double value, double plus, char algorithm);
double maxWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction);
double minWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction);
double computeWireValue(struct vde_wirefilter_conn *vde_conn, int tag, int direction);


#endif