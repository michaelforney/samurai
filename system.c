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


double get_load_average() {
	// TODO: Use Win32_PerfFormattedData_PerfOS_System.ProcessorQueueLength

	double load_average[3];

	if (getloadavg(load_average, 3) >= 1) {
		// Return the average over 1 minute if no error.
		return load_average[0];
	}

	return -0.0f;
}
