struct environment;

void parseinit(void);
void parse(struct environment *env);

/* default targets */
extern struct node **deftarg;
extern size_t ndeftarg;
