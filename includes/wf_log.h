#ifndef INCLUDE_LOG
#define INCLUDE_LOG

#include <syslog.h>
#include <errno.h>
#include <string.h>

void init_logs();
void print_log(int priority, const char *format, ...);

#define handle_error(condition, action, log, ...) do { 	\
    if (__builtin_expect(condition, 0)) {               \
        if (log != NULL) {                              \
            print_log(LOG_ERR, log, ##__VA_ARGS__);     \
        }                                               \
        action;                                         \
    }                                                   \
} while (0)

#endif
