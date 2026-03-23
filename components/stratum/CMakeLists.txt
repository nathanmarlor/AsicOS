idf_component_register(
    SRCS "stratum_api.c" "stratum_transport.c" "stratum_client.c" "stratum_job.c"
    INCLUDE_DIRS "include"
    REQUIRES "cJSON" "esp_tls" "lwip" "mining" "asic_driver"
)
