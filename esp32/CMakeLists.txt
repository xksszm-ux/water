cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(NOT IDF_VERSION_MAJOR EQUAL 6 OR NOT IDF_VERSION_MINOR EQUAL 1)
    message(FATAL_ERROR "Robot ESP32 requires ESP-IDF 6.1.x; activate the matching IDF environment.")
endif()
project(robot_esp32)
