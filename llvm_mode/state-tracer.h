#ifndef _H_STATE_TRACER_
#define _H_STATE_TRACER_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SHM_STATE_ENV_VAR "STATE_SHM_ID"

#define MVP_RADIUS_ENV_VAR "MVP_RADIUS"
#define MVP_CALIBRATION_ENV_VAR "MVP_CALIBRATION_SHM_ID"
#define AFL_OUTDIR_ENV_VAR "AFL_OUTDIR"

#define MIN_STACK_ALLOC_SIZE 64

#define TLSH_SIZE 72

#define MAX_NUM_STATES 100
#define MAX_REPETITIONS 1000

#define RATE_LIMIT 5

struct state_shared {
    unsigned int seq_len;
    unsigned int seq[MAX_NUM_STATES];
    unsigned int iterations;
};

enum { TRACER_IDLE, TRACER_ANALYZING, TRACER_DONE };

struct calibration {

    /* Set to 1 to turn on calibration */
    int enabled;

    /* Sequence of hashes from the first calibration execution */
    int initialized;
    int ref_len;
    char ref_state_seq[MAX_NUM_STATES][TLSH_SIZE+1];

    /* TLSH distances between the first execution and the subsequent ones */
    int dist_len;
    int dist[MAX_NUM_STATES*MAX_REPETITIONS];

    /* TLSH distance for MVP radius */
    int mvp_radius;

    /* Rate limiting to prevent state explosion */
    int rate_limit;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Keep these declarations in sync with the LLVM pass.  Object sizes are u64
 * because llvm.lifetime uses i64; C library byte counts use target size_t. */
void init_state_tracer(void);
void end_state_tracer(void);
void new_heap_alloc_record(void *, uint64_t);
void free_heap_alloc_record(void *, uint64_t);
void new_stack_alloc_record(void *, uint64_t);
void free_stack_alloc_record(void *, uint64_t);
void trace_calloc(void *, size_t, size_t);
void trace_realloc(void *, size_t, void *);
void trace_receive(void *, size_t);
void trace_send(void *, size_t);
void trace_recvmsg(void *);
void trace_sendmsg(void *);
void trace_read(int, void *, size_t);
void trace_write(int, void *, size_t);
void trace_fprintf(void *, void *, int);
void trace_fgets(void *, void *, int);
void trace_fread(void *, void *, size_t, size_t);
void trace_fwrite(void *, void *, size_t, size_t);
void trace_close(int);
void trace_fclose(FILE *);

#ifdef __cplusplus
}
#endif

#endif
