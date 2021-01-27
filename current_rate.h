/* a simple rate calculator for events, the purpose is to calculate the realtime speed. */

/* a circular queue of timestamps */
struct timestamp {
	double time; /* some time when something happened */
	struct timestamp *prev; /* previous timestamp, circular */
};

/* rate struct that keeps track of the speed of "updates" */
struct rate {
    struct timestamp *timestamps; /* list of timestamps */
    size_t num_timestamps; /* how many timestamps are there*/
    size_t max_timestamps; /* how many timestamps are allowed */
    double current_rate;   /* what is the average time per event */  
};

/* initialize timer with max timestamps to store, minimum is 2 */
void
rate_init(struct rate *, size_t);

/* free timestamps */
void 
rate_free(struct rate *);

/* add a timestamp */
void
rate_update(struct rate *);