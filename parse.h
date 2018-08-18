struct environment;

void parseinit(void);
void parse(struct environment *env);

/* supported ninja version */
extern const char *ninjaversion;

/* default targets */
extern struct node **deftarg;
extern size_t ndeftarg;
