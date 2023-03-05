#include "./time.h"
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <sys/timerfd.h>

/* Returns the current timestamp in microseconds */
uint64_t now_ns() {
	struct timeval v;
	gettimeofday(&v,NULL);
	return (uint64_t)(v.tv_sec*1000000000 + v.tv_usec*1000); 
}

/* Sets a timerfd, time is in nanoseconds */
void setTimer(int timefd, uint64_t time) {
	time_t seconds = time / (1000000000);
	long nseconds = time % (1000000000);
	struct itimerspec next = { { 0, 0 }, { seconds, nseconds } };
	
	timerfd_settime(timefd, 0, &next, NULL);
}
