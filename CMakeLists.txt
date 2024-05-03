cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

add_library(logc STATIC "dep/logc/src/log.c" "dep/logc/src/log.h")
target_compile_definitions(logc PRIVATE LOG_USE_COLOR)

# force cJSON to build and link statically
set(CJSON_BUILD_SHARED_LIBS OFF)
set(CJSON_OVERRIDE_BUILD_SHARED_LIBS ON)
add_subdirectory(dep/cJSON)

# use strict C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")

file(GLOB SOURCES "src/*.c")
file(GLOB HEADERS "include/*.h")
add_executable(fsautoproc ${SOURCES} ${HEADERS})

target_link_directories(fsautoproc PRIVATE dep)
target_link_libraries(fsautoproc PRIVATE logc "cjson-static" pthread)
target_include_directories(fsautoproc PRIVATE "include" "dep")

install(TARGETS fsautoproc DESTINATION bin)
