#include "./wf_time.h"
#include <time.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <stdlib.h>
#include "./wf_log.h"

/* Returns the current timestamp in microseconds */
uint64_t now_ns() {
	struct timeval v;
	gettimeofday(&v,NULL);
	return (uint64_t)(v.tv_sec*1000000000 + v.tv_usec*1000); 
}

/* Sets a timerfd, time is in nanoseconds */
void setTimer(const int timefd, const uint64_t ns_time) {
	time_t seconds = ns_time / (1000000000);
	long nseconds = ns_time % (1000000000);
	struct itimerspec next = { { 0, 0 }, { seconds, nseconds } };
	
	handle_error( timerfd_settime(timefd, 0, &next, NULL) == -1, { exit(1); }, "Error while setting timerfd: %s", strerror(errno) );
}

void disarmTimer(const int timefd) {
	static const struct itimerspec disarm_timer = { { 0, 0 }, { 0, 0 } };
	handle_error( timerfd_settime(timefd, 0, &disarm_timer, NULL) == -1, { exit(1); }, "Error while disarming timerfd: %s", strerror(errno) );
}