#include "./wf_markov.h"
#include "./wf_conn.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>


#define ADJMAPN(M, I, J, N) (M)[(I)*(N)+(J)]
#define ADJMAP(vde_conn, I, J) ADJMAPN((vde_conn)->markov.adjacency, (I), (J), (vde_conn)->markov.nodes_count)

#define ALGO_UNIFORM      0
#define ALGO_GAUSS_NORMAL 1
#define SIGMA (1.0/3.0) // more than 98% inside the bell


void markov_init(struct vde_wirefilter_conn *vde_conn) {
	markov_resize(vde_conn, 1);
	vde_conn->markov.current_node = 0;
}

static void copyAdjacency(struct vde_wirefilter_conn *vde_conn, int new_size, double *new_map) {
	for (int i=0; i<new_size; i++) {
		// Begins with an edge to itself (the node is not connected to anything)
		ADJMAPN(new_map, i, i, new_size) = 100.0;
		
		for (int j=1; j<new_size; j++) {
			int real_j = (i+j) % new_size; // Since i may not be at the first column of the adjacency map

			// The adjacency exists in the old map
			if ( (i < vde_conn->markov.nodes_count) && (real_j < vde_conn->markov.nodes_count) ) {
				ADJMAPN(new_map, i, real_j, new_size) = ADJMAP(vde_conn, i, real_j);
				ADJMAPN(new_map, i, i, new_size) -= ADJMAPN(new_map, i, real_j, new_size);
			}
		}
	}
}

/**
 * Increases or decreases the size of the Markov chain
*/
void markov_resize(struct vde_wirefilter_conn *vde_conn, int new_nodes_count) {
	if (vde_conn->markov.nodes_count == new_nodes_count) { return; }
	
	// The current number of nodes is insufficient
	if (vde_conn->markov.nodes_count < new_nodes_count) {
		// Creates new nodes
		vde_conn->markov.nodes = realloc(vde_conn->markov.nodes, new_nodes_count*(sizeof(struct markov_node *)));
		for (int i=vde_conn->markov.nodes_count; i<new_nodes_count; i++) {
			vde_conn->markov.nodes[i] = calloc(1, sizeof(MarkovNode));
		}
	} 
	else { 
		// Removes exceeding nodes
		for (int i=new_nodes_count;i<vde_conn->markov.nodes_count;i++) {
			free(vde_conn->markov.nodes[i]);
		}
		vde_conn->markov.nodes = realloc(vde_conn->markov.nodes, new_nodes_count*(sizeof(struct markov_node *)));

		// Places the current node on a valid node
		if (vde_conn->markov.current_node >= new_nodes_count) {
			vde_conn->markov.current_node = 0;
		}
	}
	
	double *new_adjacency_map = calloc(new_nodes_count*new_nodes_count, sizeof(double));
	copyAdjacency(vde_conn, new_nodes_count, new_adjacency_map);
	
	// Updates Markov information
	if (vde_conn->markov.adjacency) { free(vde_conn->markov.adjacency); }
	vde_conn->markov.adjacency = new_adjacency_map;
	vde_conn->markov.nodes_count = new_nodes_count;
}


static int parseWireValueString(char* string, double *value, double *plus, char *algorithm) {
	if (!string) { return -1; }
	int n = strlen(string);

	while ((string[n] == ' ' || string[n] == '\n' || string[n] == '\t') && n > 0) { string[n--] = '\0'; }

	// Reads algorithm (if set)
	switch (string[n]) {
		case 'u':
		case 'U':
			*algorithm = ALGO_UNIFORM;
			string[n--] = '\0';
			break;
		case 'n':
		case 'N':
			*algorithm = ALGO_GAUSS_NORMAL;
			string[n--] = '\0';
			break;
	}

	// Reads value and plus
	if (sscanf(string, "%lf+%lf", value, plus) <= 0) { return -1; }

	return 0;
}

static void setNodeValue(MarkovNode *node, int tag, int direction, double value, double plus, char algorithm) {
	node->value[tag][direction].value = value;
	node->value[tag][direction].plus = plus;
	node->value[tag][direction].algorithm = algorithm;
}

/**
 * Sets the tag's value of a node given the values as strings
 * If the value of a specific direction is given, it will be set in place of the bidirectional value (only for that direction).
*/
void setWireValue(MarkovNode *node, int tag, char *bidirectional_value_str, char *lr_value_str, char *rl_value_str) {
	double value = 0, plus = 0;
	char algorithm = ALGO_UNIFORM;

	if (parseWireValueString(bidirectional_value_str, &value, &plus, &algorithm) != -1) {
		setNodeValue(node, tag, LEFT_TO_RIGHT, value, plus, algorithm);
		setNodeValue(node, tag, RIGHT_TO_LEFT, value, plus, algorithm);
	}

	if (parseWireValueString(lr_value_str, &value, &plus, &algorithm) != -1) {
		setNodeValue(node, tag, LEFT_TO_RIGHT, value, plus, algorithm);
	}

	if (parseWireValueString(rl_value_str, &value, &plus, &algorithm) != -1) {
		setNodeValue(node, tag, RIGHT_TO_LEFT, value, plus, algorithm);
	}
}


/**
 *  Computes the maximum possible value for the configuration of a given node 
*/
double maxWireValue(MarkovNode *node, int tag, int direction) {
	return (node->value[tag][direction].value + node->value[tag][direction].plus);
}

/**
 * Computes the minimum possible value for the configuration of a given node
*/
double minWireValue(MarkovNode *node, int tag, int direction) {
	return (node->value[tag][direction].value - node->value[tag][direction].plus);
}

/**
 * Computes the value of the configuration of a given node
*/
double computeWireValue(MarkovNode *node, int tag, int direction) {
	WireValue *wv = &node->value[tag][direction];
	
	if (wv->plus == 0) {
		return wv->value;
	}

	switch (wv->algorithm) {
		case ALGO_UNIFORM:
			return wv->value + ( wv->plus * ((drand48()*2.0)-1.0) );
		case ALGO_GAUSS_NORMAL: {
			double x,y,r2;
			do {
				x = (2*drand48()) - 1;
				y = (2*drand48()) - 1;
				r2 = x*x + y*y;
			} while (r2 >= 1.0);
			return wv->value + ( wv->plus * SIGMA * x * sqrt( (-2 * log(r2)) / r2 ) );
		}
		default:
			return 0.0;
	}
}