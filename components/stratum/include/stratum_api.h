#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define STRATUM_MAX_MERKLE_BRANCHES 32

typedef struct {
    char     job_id[32];
    char     prev_block_hash[72];
    char     coinbase_1[512];
    char     coinbase_2[512];
    char     merkle_branches[STRATUM_MAX_MERKLE_BRANCHES][72];
    int      merkle_branch_count;
    char     version[12];
    char     nbits[12];
    char     ntime[12];
    bool     clean_jobs;
} stratum_notify_t;

typedef struct {
    char subscription_id[64];
    char extranonce1[32];
    int  extranonce2_size;
} stratum_subscribe_result_t;

/* Build JSON-RPC messages. Returns length written (excl. NUL), or -1 on error. */
int stratum_build_subscribe(char *buf, size_t buf_len);
int stratum_build_authorize(char *buf, size_t buf_len,
                            const char *user, const char *pass);
int stratum_build_submit(char *buf, size_t buf_len,
                         const char *user, const char *job_id,
                         const char *extranonce2, const char *ntime,
                         const char *nonce, const char *version_bits);
int stratum_build_configure(char *buf, size_t buf_len, uint32_t version_mask);
int stratum_build_suggest_difficulty(char *buf, size_t buf_len, double diff);

/* Parse server messages. Returns 0 on success, -1 on error. */
int stratum_parse_notify(const char *json, stratum_notify_t *out);
int stratum_parse_set_difficulty(const char *json, double *diff_out);
int stratum_parse_subscribe_result(const char *json, stratum_subscribe_result_t *out);
int stratum_parse_set_version_mask(const char *json, uint32_t *mask_out);

/* Detect the JSON-RPC method string. Writes to caller-provided buffer.
 * Returns 0 on success, -1 if no method field found. */
int stratum_detect_method(const char *json, char *method_out, size_t method_out_len);
