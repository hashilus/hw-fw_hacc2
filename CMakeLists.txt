# Copyright (c) 2025 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19.0)

set(MBED_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os CACHE INTERNAL "")
set(MBED_CONFIG_PATH ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(APP_TARGET hw-hacc2)

include(${MBED_PATH}/tools/cmake/app.cmake)

project(${APP_TARGET})

add_subdirectory(${MBED_PATH})

add_executable(${APP_TARGET}
    main.cpp
    SSRDriver.cpp
    RGBLEDDriver.cpp
    UDPController.cpp
    ConfigManager.cpp
    Eeprom93C46Core.cpp
    MacAddress93C46.cpp
    SerialController.cpp
    NetworkManager.cpp
)

target_link_libraries(${APP_TARGET} mbed-os mbed-netsocket mbed-storage)

# 明示的にインクルードパスを追加
target_include_directories(${APP_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os
    ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os/storage/filesystem/littlefs/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os/storage/blockdevice/COMPONENT_FLASHIAP/include
)

mbed_set_post_build(${APP_TARGET})

option(VERBOSE_BUILD "Have a verbose build process")
if(VERBOSE_BUILD)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()