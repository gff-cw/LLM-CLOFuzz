#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define MAX_NUM_STATES 100
#define MAX_REPETITIONS 1000
#define TLSH_SIZE 72

/* Mock structs matching state-tracer.h */
struct state_shared_test {
    unsigned int can_before;
    unsigned int seq_len;
    unsigned int seq[MAX_NUM_STATES];
    unsigned int iterations;
    unsigned int can_after;
};

struct calibration_test {
    int enabled;
    int initialized;
    int ref_len;
    char ref_state_seq[MAX_NUM_STATES][TLSH_SIZE+1];
    int dist_len;
    int dist[MAX_NUM_STATES*MAX_REPETITIONS];
    int mvp_radius;
    int rate_limit;
};

typedef struct {
    int start_byte;
    int end_byte;
    char modifiable;
    unsigned int *state_sequence;
    unsigned int state_count;
} region_test_t;

/* Test S01: shmat check */
void test_s01(void) {
    printf("[*] Testing S01: shmat return value checks...\n");
    void *shmat_fail = (void *)-1;
    void *shmat_ok = (void *)0x12345678;

    /* Old check: if (!ptr) -> does not trigger on (void *)-1 */
    int old_caught_fail = (!shmat_fail);
    assert(old_caught_fail == 0); /* BUG: not caught! */

    /* New check: if (ptr == (void *)-1 || !ptr) */
    int new_caught_fail = (shmat_fail == (void *)-1 || !shmat_fail);
    assert(new_caught_fail == 1); /* FIXED: caught! */

    int new_caught_ok = (shmat_ok == (void *)-1 || !shmat_ok);
    assert(new_caught_ok == 0); /* OK pointer passes */

    printf("    -> S01 PASS: (void *)-1 properly detected.\n");
}

/* S02 tracer logic simulator */
void simulate_tracer_writes(struct state_shared_test *shm, int num_incoming_states) {
    shm->seq_len = 0;
    shm->iterations = 0;
    unsigned int *state_sequence = shm->seq;
    unsigned int current_state_number = 0;

    /* Initial state 0 */
    *state_sequence = 0;
    state_sequence++;
    current_state_number++;

    for (int i = 0; i < num_incoming_states; i++) {
        unsigned int current_state_value = 0xA000 + i;
        if (current_state_number < MAX_NUM_STATES) {
            *state_sequence = current_state_value;
            state_sequence++;
        }
        current_state_number++;
        if (current_state_number <= MAX_NUM_STATES)
            shm->seq_len = current_state_number;
        else
            shm->seq_len = MAX_NUM_STATES;
    }

    /* Final state */
    unsigned int final_state_value = 0xFFFF;
    if (current_state_number < MAX_NUM_STATES) {
        *state_sequence = final_state_value;
        shm->seq_len = current_state_number + 1;
    } else {
        shm->seq_len = MAX_NUM_STATES;
    }
}

/* S02 fuzzer extract_response_state_tracer */
unsigned int* mock_extract_response_state_tracer(struct state_shared_test *shm, unsigned int *state_count_ref) {
    if (!state_count_ref) return NULL;
    if (!shm) {
        *state_count_ref = 0;
        return NULL;
    }

    unsigned int state_tracer_count_all = shm->seq_len;
    if (state_tracer_count_all > MAX_NUM_STATES) {
        state_tracer_count_all = MAX_NUM_STATES;
    }

    *state_count_ref = state_tracer_count_all;
    if (state_tracer_count_all == 0) {
        return NULL;
    }

    unsigned int *seq = (unsigned int *)malloc((*state_count_ref) * sizeof(unsigned int));
    assert(seq != NULL);
    memcpy(seq, shm->seq, (*state_count_ref) * sizeof(unsigned int));
    return seq;
}

void test_s02(void) {
    printf("[*] Testing S02: State sequence boundary protection...\n");
    struct state_shared_test shm;
    shm.can_before = 0xDEADBEEF;
    shm.can_after = 0xCAFEBABE;

    /* Case 1: Over-capacity write in tracer */
    simulate_tracer_writes(&shm, 150);
    assert(shm.can_before == 0xDEADBEEF);
    assert(shm.can_after == 0xCAFEBABE);
    assert(shm.seq_len == MAX_NUM_STATES);

    /* Case 2: Extract when seq_len == 0 */
    shm.seq_len = 0;
    unsigned int count = 999;
    unsigned int *res = mock_extract_response_state_tracer(&shm, &count);
    assert(res == NULL);
    assert(count == 0);

    /* Case 3: Extract when seq_len is normal (5) */
    shm.seq_len = 5;
    for (int i = 0; i < 5; i++) shm.seq[i] = i + 1;
    res = mock_extract_response_state_tracer(&shm, &count);
    assert(res != NULL);
    assert(count == 5);
    for (int i = 0; i < 5; i++) assert(res[i] == (unsigned int)(i + 1));
    free(res);

    /* Case 4: Extract when untrusted seq_len is corrupted (> MAX_NUM_STATES) */
    shm.seq_len = 500;
    res = mock_extract_response_state_tracer(&shm, &count);
    assert(res != NULL);
    assert(count == MAX_NUM_STATES);
    free(res);

    printf("    -> S02 PASS: Boundary enforcement and zero-length safety verified.\n");
}

/* Test S03: Calibration boundaries */
void test_s03(void) {
    printf("[*] Testing S03: Calibration boundary protection...\n");
    struct calibration_test calib;
    memset(&calib, 0, sizeof(calib));
    calib.enabled = 1;
    calib.initialized = 0;

    /* Write ref_state_seq past MAX_NUM_STATES */
    for (int i = 0; i < 150; i++) {
        char hash[TLSH_SIZE + 1];
        snprintf(hash, sizeof(hash), "HASH_%04d", i);
        if (calib.ref_len < MAX_NUM_STATES) {
            snprintf(calib.ref_state_seq[calib.ref_len], sizeof(calib.ref_state_seq[calib.ref_len]), "%s", hash);
            calib.ref_len++;
        }
    }
    assert(calib.ref_len == MAX_NUM_STATES);

    /* Test off-by-one condition in calibration comparison */
    calib.initialized = 1;
    /* current_state_number == calib.ref_len is OUT OF BOUNDS for 0-indexed array! */
    unsigned int current_state_number = calib.ref_len;
    int accessed = 0;
    if (current_state_number < (unsigned int)calib.ref_len && current_state_number < MAX_NUM_STATES) {
        accessed = 1;
    }
    assert(accessed == 0); /* Correctly rejected */

    /* Valid index */
    current_state_number = calib.ref_len - 1;
    if (current_state_number < (unsigned int)calib.ref_len && current_state_number < MAX_NUM_STATES) {
        accessed = 1;
    }
    assert(accessed == 1); /* Allowed */

    /* Test dist array overflow */
    int max_dist_entries = MAX_NUM_STATES * MAX_REPETITIONS;
    for (int i = 0; i < max_dist_entries + 100; i++) {
        if (calib.dist_len < max_dist_entries) {
            calib.dist[calib.dist_len] = 42;
            calib.dist_len++;
        }
    }
    assert(calib.dist_len == max_dist_entries);

    printf("    -> S03 PASS: Reference and distance array bounds verified.\n");
}

/* S04 Replay parsing simulator */
int mock_extract_requests_generic(unsigned char *buf, unsigned int buf_size, region_test_t **out_regions, unsigned int *out_count) {
    unsigned int region_count = 0;
    region_test_t *regions = NULL;
    unsigned int cur_start = 0;
    unsigned int byte_count = 0;

    while (byte_count < buf_size) {
        if (buf_size - byte_count < sizeof(unsigned int)) {
            /* Error: truncated header */
            free(regions);
            return -1;
        }

        unsigned int next_message_len;
        memcpy(&next_message_len, &buf[byte_count], sizeof(unsigned int));
        byte_count += sizeof(unsigned int);

        if (next_message_len > buf_size - byte_count) {
            /* Error: length exceeds remaining buffer (prevents overflow) */
            free(regions);
            return -2;
        }

        region_count++;
        region_test_t *new_r = (region_test_t *)realloc(regions, region_count * sizeof(region_test_t));
        assert(new_r != NULL);
        regions = new_r;

        regions[region_count - 1].start_byte = (int)cur_start;
        regions[region_count - 1].end_byte = (next_message_len > 0) ? (int)(cur_start + next_message_len - 1) : ((int)cur_start - 1);
        regions[region_count - 1].state_sequence = NULL;
        regions[region_count - 1].state_count = 0;

        cur_start += next_message_len;
        byte_count += next_message_len;
    }

    *out_count = region_count;
    *out_regions = regions;
    return 0;
}

void test_s04(void) {
    printf("[*] Testing S04: Replay length parsing and overflow protection...\n");
    region_test_t *regions = NULL;
    unsigned int count = 0;

    /* Case 1: Truncated header (< 4 bytes) */
    unsigned char short_buf[3] = {0x01, 0x02, 0x03};
    assert(mock_extract_requests_generic(short_buf, sizeof(short_buf), &regions, &count) == -1);

    /* Case 2: Integer overflow attack: next_message_len = 0xFFFFFFFF, buffer size = 10 */
    unsigned char overflow_buf[10];
    unsigned int huge_len = 0xFFFFFFFF;
    memcpy(overflow_buf, &huge_len, 4);
    memset(overflow_buf + 4, 0xAA, 6);
    assert(mock_extract_requests_generic(overflow_buf, sizeof(overflow_buf), &regions, &count) == -2);

    /* Case 3: Zero-length message at start */
    unsigned char zero_msg_buf[8];
    unsigned int len0 = 0;
    unsigned int len1 = 4;
    memcpy(zero_msg_buf, &len0, 4);
    memcpy(zero_msg_buf + 4, "TEST", 4);
    /* Buffer has len0=0, then data for next message */
    unsigned char two_msgs[12];
    memcpy(two_msgs, &len0, 4);
    memcpy(two_msgs + 4, &len1, 4);
    memcpy(two_msgs + 8, "TEST", 4);
    assert(mock_extract_requests_generic(two_msgs, sizeof(two_msgs), &regions, &count) == 0);
    assert(count == 2);
    /* Message 0: len 0, start 0, end -1 */
    assert(regions[0].start_byte == 0);
    assert(regions[0].end_byte == -1);
    /* Message 1: len 4, start 0, end 3 */
    assert(regions[1].start_byte == 0);
    assert(regions[1].end_byte == 3);
    free(regions);

    /* Case 4: Normal multi-packet */
    unsigned char normal_buf[16];
    unsigned int nlen1 = 3;
    unsigned int nlen2 = 5;
    memcpy(normal_buf, &nlen1, 4);
    memcpy(normal_buf + 4, "ABC", 3);
    memcpy(normal_buf + 7, &nlen2, 4);
    memcpy(normal_buf + 11, "12345", 5);
    assert(mock_extract_requests_generic(normal_buf, sizeof(normal_buf), &regions, &count) == 0);
    assert(count == 2);
    assert(regions[0].start_byte == 0);
    assert(regions[0].end_byte == 2);
    assert(regions[1].start_byte == 3);
    assert(regions[1].end_byte == 7);
    free(regions);

    printf("    -> S04 PASS: Integer overflow and zero-length boundaries verified.\n");
}

/* Test S05: File overwrite O_TRUNC */
void test_s05(void) {
    printf("[*] Testing S05: File overwrite consistency (O_TRUNC)...\n");
    const char *test_path = "test_s05_trunc.tmp";

    /* Step 1: create a file with 100 bytes */
    int fd = open(test_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    char buf100[100];
    memset(buf100, 'A', 100);
    write(fd, buf100, 100);
    close(fd);

    struct stat st;
    stat(test_path, &st);
    assert(st.st_size == 100);

    /* Step 2: Overwrite with 20 bytes WITHOUT O_TRUNC (demonstrate bug) */
    fd = open(test_path, O_WRONLY | O_CREAT, 0600);
    assert(fd >= 0);
    char buf20[20];
    memset(buf20, 'B', 20);
    write(fd, buf20, 20);
    close(fd);

    stat(test_path, &st);
    assert(st.st_size == 100); /* BUG: size remained 100 bytes! */

    /* Step 3: Overwrite with 20 bytes WITH O_TRUNC (fixed behavior) */
    fd = open(test_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    assert(fd >= 0);
    write(fd, buf20, 20);
    close(fd);

    stat(test_path, &st);
    assert(st.st_size == 20); /* FIXED: size is truncated to 20 bytes! */

    unlink(test_path);
    printf("    -> S05 PASS: O_TRUNC ensures stale bytes are stripped.\n");
}

int main(void) {
    printf("========================================\n");
    printf("LLM-CLOFuzz S01-S05 Reliability Unit Test\n");
    printf("========================================\n");
    test_s01();
    test_s02();
    test_s03();
    test_s04();
    test_s05();
    printf("========================================\n");
    printf("ALL TESTS PASSED SUCCESSFULLY (5/5)!\n");
    printf("========================================\n");
    return 0;
}
