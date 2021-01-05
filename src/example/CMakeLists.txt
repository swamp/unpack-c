cmake_minimum_required(VERSION 3.17)
project(swamp-unpack-example C)

set(CMAKE_C_STANDARD 11)

set(deps ../../deps/)



add_executable(swamp-unpack-example
        main.c
)

target_link_libraries(swamp-unpack-example swamp_unpack m)

