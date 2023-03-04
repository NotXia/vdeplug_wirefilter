#include "./time.h"
#include <time.h>
#include <sys/time.h>
#include <stdint.h>


/* Returns the current timestamp in microseconds */
uint64_t now_ns() {
	struct timeval v;
	gettimeofday(&v,NULL);
	return (uint64_t)(v.tv_sec*1000000000 + v.tv_usec*1000); 
}