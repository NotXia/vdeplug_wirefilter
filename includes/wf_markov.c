#include "./wf_markov.h"
#include <stdlib.h>
#include <math.h>
#include "./wf_conn.h"


#define ADJMAPN(M, I, J, N) (M)[(I)*(N)+(J)]
#define ADJMAP(vde_conn, I, J) ADJMAPN((vde_conn)->markov.adjacency, (I), (J), (vde_conn)->markov.nodes_count)

#define NODE_VALUE(vde_conn, N, T, D) 		( vde_conn->markov.nodes[N]->value[T][D] )
#define CURRENT_NODE_VALUE(vde_conn, T, D) 	( NODE_VALUE(vde_conn, vde_conn->markov.current_node, T, D) )

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
	
	// Update Markov information
	if (vde_conn->markov.adjacency) { free(vde_conn->markov.adjacency); }
	vde_conn->markov.adjacency = new_adjacency_map;
	vde_conn->markov.nodes_count = new_nodes_count;
}


void setWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction, double value, double plus, char algorithm) {
	vde_conn->markov.nodes[node]->value[tag][direction].value = value;
	vde_conn->markov.nodes[node]->value[tag][direction].plus = plus;
	vde_conn->markov.nodes[node]->value[tag][direction].algorithm = algorithm;
}


/**
 *  Computes the maximum value possible for the configuration of a given node 
*/
double maxWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction) {
	return (NODE_VALUE(vde_conn, node, tag, direction).value + NODE_VALUE(vde_conn, node, tag, direction).plus);
}


/**
 * Computes the minimum value possible for the configuration of a given node
*/
double minWireValue(struct vde_wirefilter_conn *vde_conn, int node, int tag, int direction) {
	return (NODE_VALUE(vde_conn, node, tag, direction).value - NODE_VALUE(vde_conn, node, tag, direction).plus);
}


/**
 * Computes the value of the current node with a given configuration
*/
double computeWireValue(struct vde_wirefilter_conn *vde_conn, int tag, int direction) {
	WireValue *wv = &CURRENT_NODE_VALUE(vde_conn, tag, direction);
	
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