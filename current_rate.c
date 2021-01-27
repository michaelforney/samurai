#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#include "util.h"
#include "current_rate.h"

static double
gettime(struct rate *r ){
    struct timespec current_time;

    if ( clock_gettime(CLOCK_MONOTONIC, &current_time) == -1){
        warn(":clock_gettime");

        /* get the most recent timestamp time */
        if(r && r->timestamps)
            return r->timestamps->time;
        else
            return 0;
        
    } else {
        return current_time.tv_sec + 0.000000001 * current_time.tv_nsec;
    }    
}

static void
free_timestamp(struct rate *r){
    struct timestamp *ll = r->timestamps;
	struct timestamp *prev; /* the oldest timestamp */

	if (ll){
        /* cut ll->prev outside the circular linked list */
		prev = ll->prev;
		ll->prev = prev->prev;
		
        /* remove it */
		free(prev);
		r->num_timestamps--;

		/* if it was the only item, there are no more items */
		if (ll == prev)
			r->timestamps = NULL;
	}
}

void
rate_free(struct rate *r){
	if (r->num_timestamps ==  0)
		return;
	while(r->timestamps)
		free_timestamp(r);
}

void
rate_update( struct rate *r ){
    double time = gettime(r); /* get the most recent time */
	struct timestamp *new_timestamp; /* new timestamp */
    /* linked list of timestamps, to avoid too many derefs */
	struct timestamp *ll = r->timestamps;
	
	/* add new timestamp */
	if (r->num_timestamps < r->max_timestamps){
		/* allocate new timestamp */
		new_timestamp = xmalloc(sizeof(struct timestamp));

		/* update previous chain, which is circular */
		if (ll){
			new_timestamp->prev = ll->prev;
			ll->prev = new_timestamp;
		} else
			new_timestamp->prev = new_timestamp;

		r->num_timestamps++;
	} else
        /* reuse oldest timestamp as the newest */
		new_timestamp = ll->prev;
	
	/* set time, update newest timestamp */
    new_timestamp->time = time;
	r->timestamps = new_timestamp;
	
	/* calculate rate, requires at least two timestamps */
	if (r->num_timestamps > 1) {
		r->current_rate = ( r->num_timestamps -1 ) / (new_timestamp->time - new_timestamp->prev->time);
	}
}

void
rate_init(struct rate *r, size_t max_timestamps){
    r->num_timestamps = 0;
    r->max_timestamps = max_timestamps < 2 ? 2 : max_timestamps;
    rate_update(r); /* the first timestamp */
}
