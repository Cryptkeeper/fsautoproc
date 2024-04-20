cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

add_library(logc STATIC "logc/src/log.c" "logc/src/log.h")
target_compile_definitions(logc PRIVATE LOG_USE_COLOR)

# use strict C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pedantic")

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

file(GLOB SOURCES "src/*.c")
file(GLOB HEADERS "include/*.h")
add_executable(fsautoproc ${SOURCES} ${HEADERS})
target_include_directories(fsautoproc PRIVATE "include" "logc/src")
target_link_libraries(fsautoproc PRIVATE logc cjson)

install(TARGETS fsautoproc DESTINATION bin)