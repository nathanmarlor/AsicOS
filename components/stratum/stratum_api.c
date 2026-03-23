#include "stratum_api.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int s_msg_id = 1;

/* ---------------------------------------------------------------------------
 * Build helpers - each returns length written (excl. NUL) or -1 on error.
 * All messages are terminated with '\n' for the Stratum line protocol.
 * -------------------------------------------------------------------------*/

int stratum_build_subscribe(char *buf, size_t buf_len)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.subscribe\",\"params\":[]}\n", id);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

int stratum_build_authorize(char *buf, size_t buf_len,
                            const char *user, const char *pass)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"%s\"]}\n",
        id, user, pass);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

int stratum_build_submit(char *buf, size_t buf_len,
                         const char *user, const char *job_id,
                         const char *extranonce2, const char *ntime,
                         const char *nonce, const char *version_bits)
{
    int id = s_msg_id++;
    int n;
    if (version_bits && version_bits[0]) {
        n = snprintf(buf, buf_len,
            "{\"id\":%d,\"method\":\"mining.submit\","
            "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id, user, job_id, extranonce2, ntime, nonce, version_bits);
    } else {
        n = snprintf(buf, buf_len,
            "{\"id\":%d,\"method\":\"mining.submit\","
            "\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
            id, user, job_id, extranonce2, ntime, nonce);
    }
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

int stratum_build_configure(char *buf, size_t buf_len, uint32_t version_mask)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.configure\","
        "\"params\":[[\"version-rolling\"],"
        "{\"version-rolling.mask\":\"%08x\",\"version-rolling.min-bit-count\":2}]}\n",
        id, version_mask);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

int stratum_build_suggest_difficulty(char *buf, size_t buf_len, double diff)
{
    int id = s_msg_id++;
    int n = snprintf(buf, buf_len,
        "{\"id\":%d,\"method\":\"mining.suggest_difficulty\",\"params\":[%g]}\n",
        id, diff);
    return (n > 0 && (size_t)n < buf_len) ? n : -1;
}

/* ---------------------------------------------------------------------------
 * Parse helpers - return 0 on success, -1 on error.
 * -------------------------------------------------------------------------*/

int stratum_detect_method(const char *json, char *method_out, size_t method_out_len)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *method = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsString(method) || !method->valuestring) {
        cJSON_Delete(root);
        return -1;
    }

    size_t len = strlen(method->valuestring);
    if (len >= method_out_len) {
        cJSON_Delete(root);
        return -1;
    }
    memcpy(method_out, method->valuestring, len + 1);

    cJSON_Delete(root);
    return 0;
}

int stratum_parse_notify(const char *json, stratum_notify_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 9) {
        cJSON_Delete(root);
        return -1;
    }

    memset(out, 0, sizeof(*out));

    /* params[0] = job_id */
    cJSON *item = cJSON_GetArrayItem(params, 0);
    if (cJSON_IsString(item))
        snprintf(out->job_id, sizeof(out->job_id), "%s", item->valuestring);

    /* params[1] = prev_block_hash */
    item = cJSON_GetArrayItem(params, 1);
    if (cJSON_IsString(item))
        snprintf(out->prev_block_hash, sizeof(out->prev_block_hash), "%s", item->valuestring);

    /* params[2] = coinbase1 */
    item = cJSON_GetArrayItem(params, 2);
    if (cJSON_IsString(item))
        snprintf(out->coinbase_1, sizeof(out->coinbase_1), "%s", item->valuestring);

    /* params[3] = coinbase2 */
    item = cJSON_GetArrayItem(params, 3);
    if (cJSON_IsString(item))
        snprintf(out->coinbase_2, sizeof(out->coinbase_2), "%s", item->valuestring);

    /* params[4] = merkle_branches array */
    cJSON *branches = cJSON_GetArrayItem(params, 4);
    if (cJSON_IsArray(branches)) {
        int count = cJSON_GetArraySize(branches);
        if (count > STRATUM_MAX_MERKLE_BRANCHES)
            count = STRATUM_MAX_MERKLE_BRANCHES;
        out->merkle_branch_count = count;
        for (int i = 0; i < count; i++) {
            cJSON *b = cJSON_GetArrayItem(branches, i);
            if (cJSON_IsString(b))
                snprintf(out->merkle_branches[i], sizeof(out->merkle_branches[i]),
                         "%s", b->valuestring);
        }
    }

    /* params[5] = version */
    item = cJSON_GetArrayItem(params, 5);
    if (cJSON_IsString(item))
        snprintf(out->version, sizeof(out->version), "%s", item->valuestring);

    /* params[6] = nbits */
    item = cJSON_GetArrayItem(params, 6);
    if (cJSON_IsString(item))
        snprintf(out->nbits, sizeof(out->nbits), "%s", item->valuestring);

    /* params[7] = ntime */
    item = cJSON_GetArrayItem(params, 7);
    if (cJSON_IsString(item))
        snprintf(out->ntime, sizeof(out->ntime), "%s", item->valuestring);

    /* params[8] = clean_jobs */
    item = cJSON_GetArrayItem(params, 8);
    out->clean_jobs = cJSON_IsTrue(item);

    cJSON_Delete(root);
    return 0;
}

int stratum_parse_set_difficulty(const char *json, double *diff_out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) < 1) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON *item = cJSON_GetArrayItem(params, 0);
    if (!cJSON_IsNumber(item)) {
        cJSON_Delete(root);
        return -1;
    }

    *diff_out = item->valuedouble;
    cJSON_Delete(root);
    return 0;
}

int stratum_parse_subscribe_result(const char *json, stratum_subscribe_result_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    memset(out, 0, sizeof(*out));

    /* Result is in "result" array: [ [ [sub_type, sub_id], ... ], extranonce1, en2_size ] */
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (!cJSON_IsArray(result) || cJSON_GetArraySize(result) < 3) {
        cJSON_Delete(root);
        return -1;
    }

    /* Subscription details - first element is array of subscriptions */
    cJSON *subs = cJSON_GetArrayItem(result, 0);
    if (cJSON_IsArray(subs) && cJSON_GetArraySize(subs) > 0) {
        cJSON *first_sub = cJSON_GetArrayItem(subs, 0);
        if (cJSON_IsArray(first_sub) && cJSON_GetArraySize(first_sub) >= 2) {
            cJSON *sub_id = cJSON_GetArrayItem(first_sub, 1);
            if (cJSON_IsString(sub_id))
                snprintf(out->subscription_id, sizeof(out->subscription_id),
                         "%s", sub_id->valuestring);
        }
    }

    /* Extranonce1 */
    cJSON *en1 = cJSON_GetArrayItem(result, 1);
    if (cJSON_IsString(en1))
        snprintf(out->extranonce1, sizeof(out->extranonce1), "%s", en1->valuestring);

    /* Extranonce2 size */
    cJSON *en2_size = cJSON_GetArrayItem(result, 2);
    if (cJSON_IsNumber(en2_size))
        out->extranonce2_size = en2_size->valueint;

    cJSON_Delete(root);
    return 0;
}

int stratum_parse_set_version_mask(const char *json, uint32_t *mask_out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;

    /* Can come as mining.set_version_mask with params[0] = mask string,
     * or as a mining.configure response with result.version-rolling.mask */
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (cJSON_IsArray(params) && cJSON_GetArraySize(params) >= 1) {
        cJSON *item = cJSON_GetArrayItem(params, 0);
        if (cJSON_IsString(item)) {
            *mask_out = (uint32_t)strtoul(item->valuestring, NULL, 16);
            cJSON_Delete(root);
            return 0;
        }
    }

    /* Also check result object for configure response */
    cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsObject(result)) {
        cJSON *mask = cJSON_GetObjectItemCaseSensitive(result, "version-rolling.mask");
        if (cJSON_IsString(mask)) {
            *mask_out = (uint32_t)strtoul(mask->valuestring, NULL, 16);
            cJSON_Delete(root);
            return 0;
        }
    }

    cJSON_Delete(root);
    return -1;
}
