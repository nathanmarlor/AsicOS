cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "components/serial"
    "components/asic_driver"
    "components/stratum"
    "components/power"
    "components/mining"
    "components/nvs_config"
    "components/auto_tuner"
    "components/self_test"
    "components/remote_access"
    "components/display"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(AsicOS)
