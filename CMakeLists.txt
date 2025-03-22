cmake_minimum_required(VERSION 3.28.0)
project(cqy VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)  
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    if (CMAKE_CXX_COMPILER_VERSION MATCHES "12.*")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
    endif()
    # sudo apt update && sudo apt upgrade -y
    # sudo apt install openssl libssl-dev libssl-doc
    set(YLT_ENABLE_SSL ON)
    add_compile_options(-fsanitize=address -g)
    link_libraries(asan)
elseif()
    add_compile_options("/wd4819")
    add_compile_options("/wd4267")
endif()

include_directories(3rd/yalantinglibs/src/include)
link_libraries(yalantinglibs::yalantinglibs)

add_library(cqy STATIC cqy.h cqy.cpp)
target_include_directories(cqy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

link_libraries(cqy)
add_executable(ping example/ping.cpp)
add_executable(pong example/pong.cpp)
add_executable(wsgate example/wsgate.cpp)
add_executable(test test/test.cpp)

add_test(NAME tests COMMAND test)
add_subdirectory(3rd/yalantinglibs)