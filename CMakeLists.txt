cmake_minimum_required(VERSION 3.22)

project(fsautoproc C)

add_library(logc STATIC "logc/src/log.c" "logc/src/log.h")
target_compile_definitions(logc PRIVATE LOG_USE_COLOR)

include_directories(/usr/local/include)
link_directories(/usr/local/lib)

file(GLOB SOURCES "*.c")
add_executable(fsautoproc ${SOURCES})
target_include_directories(fsautoproc PRIVATE "logc/src")
target_link_libraries(fsautoproc PRIVATE logc cjson)

install(TARGETS fsautoproc DESTINATION bin)