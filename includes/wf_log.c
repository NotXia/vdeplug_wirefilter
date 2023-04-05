#include "./wf_log.h"
#include <stdio.h>
#include <stdarg.h>

void init_logs() {
	openlog("wirefilter", LOG_PID, 0);
}

void print_log(int priority, const char *format, ...) {
	va_list arg;
	va_start(arg, format);

    vsyslog(priority, format, arg);

	va_end(arg);
}