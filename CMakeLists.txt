cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

# force cJSON to build and link statically
set(CJSON_BUILD_SHARED_LIBS OFF)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON)
set(ENABLE_CJSON_TEST OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(dep/cJSON)

# use strict C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")

find_program(IWYU_PATH NAMES include-what-you-use iwyu)
if (IWYU_PATH)
    # TODO: use --error to fail the build if IWYU fails, but Ubuntu 20.04's IWYU is too old
    set(CMAKE_C_INCLUDE_WHAT_YOU_USE ${IWYU_PATH} -Xiwyu --mapping_file=${CMAKE_SOURCE_DIR}/.iwyu.imp)
endif ()

file(GLOB SOURCES "src/*.c")
file(GLOB HEADERS "include/*.h")
add_executable(fsautoproc ${SOURCES} ${HEADERS})

target_link_directories(fsautoproc PRIVATE dep)
target_link_libraries(fsautoproc PRIVATE cjson pthread)
target_include_directories(fsautoproc PRIVATE include dep)
set_target_properties(fsautoproc PROPERTIES PUBLIC_HEADER "${HEADERS}")

install(TARGETS fsautoproc DESTINATION bin)

# libdeng shared library for unit tests
add_library(deng STATIC src/deng.c src/index.c src/fs.c)
target_include_directories(deng PUBLIC include dep)

# unit tests
enable_testing()

add_executable(test_deng test/test_deng.c)
target_link_libraries(test_deng PRIVATE deng)
add_test(NAME deng COMMAND test_deng)
