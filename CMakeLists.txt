cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# dependencies.lock contient des chemins Windows : lock séparé hors Windows
if(NOT CMAKE_HOST_WIN32)
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.linux.lock")
endif()

project(BabyBot)
