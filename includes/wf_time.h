#ifndef INCLUDE_TIME
#define INCLUDE_TIME

#define MS_TO_NS(ms) ((ms) * 1000000)
#define NS_TO_MS(ns) ((ns) / 1000000)
#define NS_TO_US(ns) ((ns) / 1000)

unsigned long long now_ns();

#endif