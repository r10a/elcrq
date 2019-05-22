cmake_minimum_required(VERSION 2.8)
project(c C)

set(CMAKE_C_COMPILER "gcc")

set(CMAKE_C_FLAGS "-std=gnu99 -g -Wall -O3 -D_GNU_SOURCE -DSHM -DLOCKTYPE=SPINLOCK -DPSHARED")

find_package( Threads REQUIRED )

add_executable(c malloc.c malloc.h elcrq.h main.c EventCount.h primitives.h)
#add_executable(c elcrq.c)

target_link_libraries(c -lm -lpthread -lrt)