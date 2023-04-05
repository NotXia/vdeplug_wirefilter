#include <syslog.h>

void init_logs();
void print_log(int priority, const char *format, ...);