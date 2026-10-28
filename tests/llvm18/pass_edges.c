/* Opaque-pointer evidence from callers, callee bodies and debug pointer slots. */
struct edge_record { unsigned char payload[256]; unsigned tag; };
extern void edge_external(struct edge_record *);
extern struct edge_record *edge_factory(void);

void edge_body(struct edge_record *p) { p->tag++; }
void edge_formal(struct edge_record *p) { edge_body(p); }
void edge_caller_gep(struct edge_record *p) { edge_external(p); p->tag++; }
void edge_debug_formal(struct edge_record *p) { edge_external(p); }
void edge_debug_local(void) {
  struct edge_record *p = edge_factory();
  edge_external(p);
}

struct backing_record { char payload[512]; };
void edge_cast(void) {
  struct backing_record backing;
  edge_body((struct edge_record *)&backing);
}
