#include "./time.h"
#include <time.h>
#include <sys/time.h>


/* Returns the current timestamp in microseconds */
unsigned long long now_ns() {
	struct timeval v;
	gettimeofday(&v,NULL);
	return (unsigned long long)(v.tv_sec*1000000000 + v.tv_usec*1000); 
}