cmake_minimum_required(VERSION 3.28.0)
project(cqy VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)  
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# sub cmake will not cover option
cmake_policy(SET CMP0077 NEW)
set(YLT_ENABLE_SSL ON)
set(MSGPACK_USE_BOOST OFF)

message(STATUS "CMAKE_CXX_COMPILER_ID IS ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_BUILD_TYPE IS ${CMAKE_BUILD_TYPE}")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
    if (CMAKE_CXX_COMPILER_VERSION MATCHES "12.*")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
    endif()
    # sudo apt update && sudo apt upgrade -y
    # sudo apt install openssl libssl-dev libssl-doc
    add_compile_options(-fsanitize=address -g)
    link_libraries(asan)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-ftime-trace)
    endif()
    add_compile_options(-fsanitize=address -g)
    link_libraries(asan)
elseif()
    add_compile_options("/wd4819")
    add_compile_options("/wd4267")
endif()

include_directories(3rd/yalantinglibs/src/include)
link_libraries(yalantinglibs::yalantinglibs)

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

link_libraries(cqy)
add_executable(ping example/ping.cpp)
add_executable(pong example/pong.cpp)
add_executable(wsgate example/wsgate.cpp)
target_link_libraries(wsgate PRIVATE msgpack-cxx)
# add_executable(msgpack_rpc example/msgpack_rpc.cpp)
# target_link_libraries(msgpack_rpc PRIVATE msgpack-cxx)
add_executable(msgpack_codec example/msgpack_codec.cpp)
target_link_libraries(msgpack_codec PRIVATE msgpack-cxx)

add_executable(test test/test.cpp)

add_test(NAME tests COMMAND test)
add_subdirectory(3rd/yalantinglibs)
add_subdirectory(3rd/msgpack-c)