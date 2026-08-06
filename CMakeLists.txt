# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iot_house)

# Package the web UI into the LittleFS partition so it is available at runtime.
littlefs_create_partition_image(webfs "${CMAKE_CURRENT_SOURCE_DIR}/web" FLASH_IN_PROJECT)
