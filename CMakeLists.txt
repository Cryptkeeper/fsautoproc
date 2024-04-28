cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

add_library(logc STATIC "logc/src/log.c" "logc/src/log.h")
target_compile_definitions(logc PRIVATE LOG_USE_COLOR)

# use strict C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")
set(CMAKE_C_INCLUDE_WHAT_YOU_USE "include-what-you-use")

# find cJSON using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(CJSON REQUIRED libcjson)

file(GLOB SOURCES "src/*.c")
file(GLOB HEADERS "include/*.h")
add_executable(fsautoproc ${SOURCES} ${HEADERS})

target_include_directories(fsautoproc PRIVATE "include" "logc/src" ${CJSON_INCLUDE_DIRS})
target_link_libraries(fsautoproc PRIVATE logc ${CJSON_LIBRARIES})

install(TARGETS fsautoproc DESTINATION bin)
