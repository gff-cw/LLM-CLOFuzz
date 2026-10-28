struct edge_record { char payload[256]; unsigned tag; };
#ifdef DOUBLE_POINTER
extern void edge_opaque(struct edge_record **);
void probe(struct edge_record **p) { edge_opaque(p); }
#elif defined(NON_POINTER)
extern void edge_opaque(int);
void probe(int p) { edge_opaque(p); }
#else
extern void edge_opaque(void *);
void probe(void *p) { edge_opaque(p); }
#endif
