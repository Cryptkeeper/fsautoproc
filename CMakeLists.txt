cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

# force cJSON to build and link statically
set(CJSON_BUILD_SHARED_LIBS OFF)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON)
set(ENABLE_CJSON_TEST OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(dep/cJSON)

# link xxHash statically
set(XXHASH_BUILD_XXHSUM OFF)
option(BUILD_SHARED_LIBS OFF)
add_subdirectory(dep/xxHash/build/cmake xxhash_build EXCLUDE_FROM_ALL)

# link zlib statically
set(ZLIB_BUILD_TESTING OFF)
set(ZLIB_BUILD_SHARED OFF)
set(ZLIB_INSTALL OFF)
add_subdirectory(dep/zlib zlib_build EXCLUDE_FROM_ALL)

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

# building on Linux seems to auto demangle (e.g. je_free -> free) symbols, and ALSO undef the mangled ones
# this allows jemalloc to provide demangled names, but also retain the prefixed ones
add_compile_definitions(JEMALLOC_NO_DEMANGLE)

target_link_directories(fsautoproc PRIVATE dep dep/jemalloc/lib)
target_link_libraries(fsautoproc PRIVATE cjson pthread libjemalloc.a xxhash zlibstatic)
target_include_directories(fsautoproc PRIVATE include dep dep/jemalloc/include dep/zlib ${CMAKE_BINARY_DIR}/zlib_build)

install(TARGETS fsautoproc DESTINATION bin)

# libdeng shared library for unit tests
add_library(deng STATIC src/deng.c src/index.c src/fs.c src/je.c src/xx.c)

target_link_directories(deng PUBLIC dep/jemalloc/lib)
target_link_libraries(deng PUBLIC libjemalloc.a xxhash zlibstatic)
target_include_directories(deng PUBLIC include dep dep/jemalloc/include dep/zlib ${CMAKE_BINARY_DIR}/zlib_build)

# unit tests
enable_testing()

add_executable(test_deng test/test_deng.c)
target_link_libraries(test_deng PRIVATE deng)
add_test(NAME deng COMMAND test_deng)

add_executable(test_xx test/test_xx.c src/xx.c)
target_link_libraries(test_xx PRIVATE xxhash)
target_include_directories(test_xx PRIVATE include dep)
add_test(NAME xx COMMAND test_xx)