#include <string.h>
#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "nvs_config";
static const char *NVS_NAMESPACE = "asicos";

esp_err_t nvs_config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition corrupted or version mismatch, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

void nvs_config_get_string(const char *key, char *out, size_t out_len, const char *default_val)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        strncpy(out, default_val, out_len);
        out[out_len - 1] = '\0';
        return;
    }

    size_t len = out_len;
    err = nvs_get_str(handle, key, out, &len);
    if (err != ESP_OK) {
        strncpy(out, default_val, out_len);
        out[out_len - 1] = '\0';
    }

    nvs_close(handle);
}

esp_err_t nvs_config_set_string(const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

uint16_t nvs_config_get_u16(const char *key, uint16_t default_val)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_val;
    }

    uint16_t value = default_val;
    err = nvs_get_u16(handle, key, &value);
    if (err != ESP_OK) {
        value = default_val;
    }

    nvs_close(handle);
    return value;
}

esp_err_t nvs_config_set_u16(const char *key, uint16_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u16(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

uint64_t nvs_config_get_u64(const char *key, uint64_t default_val)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return default_val;
    }

    uint64_t value = default_val;
    err = nvs_get_u64(handle, key, &value);
    if (err != ESP_OK) {
        value = default_val;
    }

    nvs_close(handle);
    return value;
}

esp_err_t nvs_config_set_u64(const char *key, uint64_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u64(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
