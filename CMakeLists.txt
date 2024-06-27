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

file(GLOB SOURCES "src/*.c")
file(GLOB HEADERS "include/*.h")
add_executable(fsautoproc ${SOURCES} ${HEADERS})

target_link_directories(fsautoproc PRIVATE dep)
target_link_libraries(fsautoproc PRIVATE cjson pthread)
target_include_directories(fsautoproc PRIVATE "include" "dep")

install(TARGETS fsautoproc DESTINATION bin)
