cmake_minimum_required(VERSION 3.17)
project(swamp_unpack C)

set(CMAKE_C_STANDARD 99)

set(deps ../deps/)

add_library(swamp_unpack
        ${deps_src}
        lib/unpack.c)

target_include_directories(swamp_unpack PUBLIC include)
target_include_directories(swamp_unpack PRIVATE ${deps}/raff-c/src/include)
target_include_directories(swamp_unpack PRIVATE ${deps}/typeinfo-c/src/include)
target_include_directories(swamp_unpack PRIVATE ${deps}/flood-c/src/include)
target_include_directories(swamp_unpack PRIVATE ${deps}/tiny-libc/src/include)
target_include_directories(swamp_unpack PRIVATE ${deps}/clog/src/include)
target_include_directories(swamp_unpack PUBLIC ${deps}/runtime-c/src/include)


file(GLOB_RECURSE deps_src FOLLOW_SYMLINKS
        "${deps}*/src/lib/*.c"
        )

target_link_libraries(swamp_unpack m)
