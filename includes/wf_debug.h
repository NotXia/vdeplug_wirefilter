#ifndef INCLUDE_DEBUG
#define INCLUDE_DEBUG


#define DEBUG_ON

#ifdef DEBUG_ON
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>

	#define DEBUG_LOGS "/tmp/wfdebug_logs"

	#define WF_DEBUG_PRINT(tag, format, ...) do { 	\
		FILE *fin = fopen(tag, "a"); 				\
		fprintf(fin, format, ##__VA_ARGS__); 		\
		fclose(fin); 								\
	} while (0)
#else
	#define WF_DEBUG_PRINT(tag, format, ...)
#endif


#endif