#include "./wf_markov.h"
#include "./wf_conn.h"
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "./wf_time.h"
#include <sys/timerfd.h>
#include "./wf_debug.h"

#define ADJMAPN(M, I, J, N) (M)[(I)*(N)+(J)]
#define ADJMAP(vde_conn, I, J) ADJMAPN((vde_conn)->markov.adjacency, (I), (J), (vde_conn)->markov.nodes_count)

void markov_init(struct vde_wirefilter_conn *vde_conn, int size, int start_node, uint64_t change_frequency) {
	markov_resize(vde_conn, size <= 0 ? 1 : size);
	vde_conn->markov.current_node = start_node;
	vde_conn->markov.timerfd = timerfd_create(CLOCK_REALTIME, 0);
	vde_conn->markov.change_frequency = change_frequency;
}

static void markov_rebalance_node(struct vde_wirefilter_conn *vde_conn, int node) {
	ADJMAP(vde_conn, node, node) = 100.0;

	for (int i=1; i<vde_conn->markov.nodes_count; i++) {
		ADJMAP(vde_conn, node, node) -= ADJMAP(vde_conn, node, (node + i) % vde_conn->markov.nodes_count);
	}
}

void markov_setEdges(struct vde_wirefilter_conn *vde_conn, char *edges_str) {
	int start_node, end_node;
	double weight;

	while (*edges_str != '\0') {
		while ((*edges_str == ' ' || *edges_str == '\n' || *edges_str == '\t') && *edges_str != '\0') { edges_str++; }
		if (*edges_str == '\0') { break; }

		sscanf(edges_str, "%lf[%d][%d]", &weight, &start_node, &end_node);
		ADJMAP(vde_conn, start_node, end_node) = weight;
		markov_rebalance_node(vde_conn, start_node);

		// Moves to the next edge value
		while (*edges_str != ' ' && *edges_str != '\0') { edges_str++; }
	}
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


void markov_step(struct vde_wirefilter_conn *vde_conn, const int start_node) {
	double probability = drand48() * 100;
	int new_node = 0;
	
	for (int j=0; j<vde_conn->markov.nodes_count; j++) {
		new_node = (start_node + j) % vde_conn->markov.nodes_count;
		double change_probability = ADJMAP(vde_conn, start_node, new_node);
		
		if (change_probability >= probability) {
			break;
		}
		else {
			probability -= change_probability;
		}
	}

	vde_conn->markov.current_node = new_node;
}


static int parseWireValueString(char* string, double *value, double *plus, char *algorithm, int *to_set_node) {
	if (!string) { return -1; }
	int n = strlen(string) - 1;

	// Default values
	*value = 0, *plus = 0;
	*algorithm = ALGO_UNIFORM;
	int multiplier = 1;
	*to_set_node = 0;

	while ((string[n] == ' ' || string[n] == '\n' || string[n] == '\t') && n > 0) { string[n--] = '\0'; }

	// Reads Markov node
	if (string[n] == ']') {
		string[n--] = '\0';
		while (string[n] != '[' && n > 0) { n--; }
		if (string[n] != '[') { return -1; }

		char *node_number_str = (&string[n]) + 1;
		sscanf(node_number_str, "%d", to_set_node);
		string[n] = '\0';
	}

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

	// Reads multiplier (if set)
	switch (string[n]) {
		case 'k': 
		case 'K':
			multiplier = KILO; break;
		case 'm': 
		case 'M':
			multiplier = MEGA; break;
		case 'g': 
		case 'G':
			multiplier = GIGA; break;
		default:
			multiplier = 1; break;
	}

	// Reads value and plus
	if (sscanf(string, "%lf+%lf", value, plus) <= 0) { return -1; }
	*value = *value * multiplier;

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
void setWireValue(struct vde_wirefilter_conn *vde_conn, int tag, char *value_str, int flags) {
	double value = 0, plus = 0;
	char algorithm = ALGO_UNIFORM;
	char *value_end;
	char old_char;
	char direction = BIDIRECTIONAL;
	int to_set_node = 0;

	while (value_str && *value_str != '\0') {
		// Left trim
		while ((*value_str == ' ' || *value_str == '\n' || *value_str == '\t') && *value_str != '\0') { value_str++; }
		if (*value_str == '\0') { break; }

		// Determines the direction to set
		if ((*value_str == 'L' || *value_str == 'l') && (*(value_str+1) == 'R' || *(value_str+1) == 'r')) {
			value_str += 2;
			direction = LEFT_TO_RIGHT;
		}
		else if ((*value_str == 'R' || *value_str == 'r') && (*(value_str+1) == 'L' || *(value_str+1) == 'l')) {
			value_str += 2;
			direction = RIGHT_TO_LEFT;
		}
		else {
			direction = BIDIRECTIONAL;
		}
		if (flags & WIRE_BIDIRECTIONAL) { direction = BIDIRECTIONAL; }

		// Finds the end of the argument
		value_end = value_str;
		while (*value_end != ' ' && *value_end != '\0') { value_end++; }
		old_char = *value_end;
		*value_end = '\0';

		// Parses the value
		if (parseWireValueString(value_str, &value, &plus, &algorithm, &to_set_node) == 0) {
			if (to_set_node < 0 || to_set_node >= vde_conn->markov.nodes_count) { return; }

			if (direction == LEFT_TO_RIGHT || direction == BIDIRECTIONAL) {
				setNodeValue(vde_conn->markov.nodes[to_set_node], tag, LEFT_TO_RIGHT, value, plus, algorithm);
			}
			if (direction == RIGHT_TO_LEFT || direction == BIDIRECTIONAL) {
				setNodeValue(vde_conn->markov.nodes[to_set_node], tag, RIGHT_TO_LEFT, value, plus, algorithm);
			}
		}

		// Restores the string
		*value_end = old_char;
		value_str = value_end;
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