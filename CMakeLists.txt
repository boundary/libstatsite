cmake_minimum_required(VERSION 2.8)
project(libstatsite C)

include_directories(include/statsite)

set(SS_SOURCES src/cm_quantile.c
        src/counter.c
        src/hashmap.c
        src/heap.c
        src/hll.c
        src/hll_constants.c
        src/ini.c
        src/metrics.c
        src/MurmurHash3.c
        src/radix.c
        src/set.c
        src/timer.c)

if (${CMAKE_SYSTEM_NAME} MATCHES "(Solaris|SunOS)")
    add_definitions(-D__EXTENSIONS__)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    add_definitions(-Dlinux -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_SOURCE -D_GNU_SOURCE)
endif()

add_library(statsite STATIC ${SS_SOURCES})
target_include_directories(statsite INTERFACE include)
