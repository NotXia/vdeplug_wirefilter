#ifndef INCLUDE_TIME
#define INCLUDE_TIME

#include <stdint.h>

#define MS_TO_NS(ms) ((ms) * 1000000)
#define NS_TO_MS(ns) ((ns) / 1000000)
#define NS_TO_US(ns) ((ns) / 1000)

uint64_t now_ns();
void setTimer(const int timefd, const uint64_t ns_time);
void disarmTimer(const int timefd);

#endif