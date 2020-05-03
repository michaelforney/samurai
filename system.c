#include "system.h"

#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <unistd.h>

int get_cores_count() {
	#ifdef _SC_NPROCESSORS_ONLN
		return sysconf(_SC_NPROCESSORS_ONLN);
	#else
		return -1;
	#endif
}
