
cmake_minimum_required(VERSION 2.8)

project(h2x)

find_library(S2N_LIB_PATH s2n PATH_SUFFIXES lib)
if(NOT S2N_LIB_PATH)
    message(FATAL_ERROR "Could not find s2n library")
endif()
message(STATUS "S2N lib path: ${S2N_LIB_PATH}")

find_file(S2N_INCLUDE_FILE_PATH s2n.h PATH_SUFFIXES api)
if(NOT S2N_INCLUDE_FILE_PATH)
    message(FATAL_ERROR "Could not find s2n include")
endif()
get_filename_component(S2N_INCLUDE_PATH ${S2N_INCLUDE_FILE_PATH} DIRECTORY)
message(STATUS "S2N include path: ${S2N_INCLUDE_PATH}")


file(GLOB H2X_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/*.c")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/source ${S2N_INCLUDE_PATH})

add_executable(h2x ${H2X_SOURCE} source/h2x_frame.h)

target_compile_options(h2x PUBLIC -std=gnu99)
 
target_link_libraries(h2x ${S2N_LIB_PATH} pthread crypto rt)
