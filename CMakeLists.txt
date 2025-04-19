cmake_minimum_required(VERSION 3.28.0)
project(cqy VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)  
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(YLT_ENABLE_SSL ON CACHE BOOL "enable ssl")
set(YLT_ENABLE_SSL ON)
set(MSGPACK_USE_BOOST OFF CACHE BOOL "no boost")
set(MSGPACK_USE_BOOST OFF)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Support ccache")
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
else()
    message(STATUS "Not support ccache")
endif()

message(STATUS "CMAKE_CXX_COMPILER_ID IS ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_BUILD_TYPE IS ${CMAKE_BUILD_TYPE}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    if (CMAKE_CXX_COMPILER_VERSION MATCHES "12.*")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
    endif()
    # sudo apt update && sudo apt upgrade -y
    # sudo apt install openssl libssl-dev libssl-doc
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        # add_compile_options(-O3)
    else()
        add_compile_options(-fsanitize=address -g)
        link_libraries(asan)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    #     add_compile_options(-ftime-trace)
    # endif()
    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        # add_compile_options(-O3)
    else()
        add_compile_options(-fsanitize=address -g)
        link_libraries(asan)
    endif()
else()
    add_compile_options("/wd4819")
    add_compile_options("/wd4267")
    add_compile_options("/utf-8")
endif()

add_library(cqy STATIC 
    include/cqy_utils.h include/cqy_logger.h
    include/cqy_algo.h src/cqy_algo.cpp
    include/cqy_ctx_mgr.h src/cqy_ctx_mgr.cpp
    include/cqy_ctx.h src/cqy_ctx.cpp
    include/cqy_node.h src/cqy_node.cpp
    include/cqy_msg.h src/cqy_msg.cpp
    include/cqy_handle.h
    include/cqy_app.h src/cqy_app.cpp
    include/cqy.h
)
target_include_directories(cqy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(cqy PUBLIC 3rd/yalantinglibs/src/include)
target_link_libraries(cqy PUBLIC yalantinglibs::yalantinglibs)

add_executable(ping example/ping.cpp)
target_link_libraries(ping PRIVATE cqy)

add_executable(pong example/pong.cpp)
target_link_libraries(pong PRIVATE cqy)

add_executable(game_demo 
    example/game_demo.cpp 
    example/entity.h example/entity.cpp
    example/game_component.h example/game_component.cpp
    )
target_link_libraries(game_demo PRIVATE cqy msgpack-cxx)

add_executable(rpc_bench_client test/rpc_bench_client.cpp)
target_link_libraries(rpc_bench_client PRIVATE cqy)

add_executable(rpc_bench_server test/rpc_bench_server.cpp)
target_link_libraries(rpc_bench_server PRIVATE cqy)

set(TEST_MAIN cqy_test)
add_executable(${TEST_MAIN} test/test.cpp example/entity.cpp)
target_link_libraries(${TEST_MAIN} PRIVATE cqy)

enable_testing()
add_test(NAME tests COMMAND ${TEST_MAIN})

include(cmake/CodeCoverage.cmake)
add_subdirectory(3rd/yalantinglibs)
add_subdirectory(3rd/msgpack-c)