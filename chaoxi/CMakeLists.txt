# find_program(CLANG_TIDY_EXE clang-tidy REQUIRED)
# # 设置该目录下所有 Target 自动启用 clang-tidy 检查
# set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")

# ==========================
# 1. Base 模块 (底层的工具、日志、线程池等)
# ==========================
function(chaoxi_target_enable_warnings target)
    if(NOT CHAOXI_ENABLE_STRICT_WARNINGS)
        return()
    endif()

    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wextra>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wpedantic>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wconversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wsign-conversion>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wshadow>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wformat=2>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wcast-qual>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wcast-align>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wundef>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wnon-virtual-dtor>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Woverloaded-virtual>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<CXX_COMPILER_ID:MSVC>:/permissive->
        $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>
    )

    if(CHAOXI_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Werror>
            $<$<CXX_COMPILER_ID:MSVC>:/WX>
        )
    endif()
endfunction()

function(chaoxi_target_suppress_warnings target)
    foreach(warning IN LISTS ARGN)
        if(warning STREQUAL "deprecated-declarations")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations>
                $<$<CXX_COMPILER_ID:MSVC>:/wd4996>
            )
        elseif(warning STREQUAL "unused-parameter")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-unused-parameter>
                $<$<CXX_COMPILER_ID:MSVC>:/wd4100>
            )
        elseif(warning STREQUAL "shadow")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-shadow>
                $<$<CXX_COMPILER_ID:MSVC>:/wd4456>
                $<$<CXX_COMPILER_ID:MSVC>:/wd4457>
            )
        elseif(warning STREQUAL "sign-conversion")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-sign-conversion>
            )
        elseif(warning STREQUAL "conversion")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-conversion>
            )
        elseif(warning STREQUAL "format")
            target_compile_options(${target} PRIVATE
                $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-format>
            )
        else()
            message(WARNING "chaoxi_target_suppress_warnings: unknown warning '${warning}' for target ${target}")
        endif()
    endforeach()
endfunction()

# ==========================
# Base Library
# ==========================
add_library(chaoxi_base STATIC)
add_library(chaoxi::base ALIAS chaoxi_base)
set_target_properties(
    chaoxi_base
    PROPERTIES
    EXPORT_NAME base
)
target_sources(
    chaoxi_base

    PRIVATE
    base/Logging.cpp
    base/LogStream.cpp
    base/CurrentThread.cpp
    base/LogFile.cpp
    base/FileUtil.cpp
    base/ProcessInfo.cpp
    base/AsyncLogging.cpp

    PUBLIC
    FILE_SET HEADERS
    TYPE HEADERS

    BASE_DIRS
    "${PROJECT_SOURCE_DIR}"

    FILES
    base/AsyncLogging.hpp
    base/BlockingQueue.hpp
    base/BoundedBlockingQueue.hpp
    base/CurrentThread.hpp
    base/FileUtil.hpp
    # base/GzipFile.hpp   
    base/LogFile.hpp
    base/LogStream.hpp
    base/Logging.hpp
    base/ProcessInfo.hpp
    base/ThreadLocalSingleton.hpp
    base/Timestamp.hpp
    base/Utility.hpp
)

# add_library(chaoxi_base STATIC
#     base/Logging.cpp
#     base/LogStream.cpp
#     base/CurrentThread.cpp
#     base/LogFile.cpp
#     base/FileUtil.cpp
#     base/ProcessInfo.cpp
#     base/AsyncLogging.cpp
# )
target_compile_features(
    chaoxi_base
    PUBLIC
    cxx_std_23
)
chaoxi_target_enable_warnings(chaoxi_base)

# target_include_directories(chaoxi_base
#     PUBLIC
#     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
#     # $<INSTALL_INTERFACE:include>
# )

# 基础库可能用到了线程
find_package(Threads REQUIRED)
target_link_libraries(chaoxi_base PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(chaoxi_base PUBLIC stdc++exp)
endif()
if(WIN32)
    target_link_libraries(chaoxi_base PUBLIC advapi32)
endif()



# ==========================
# Net Library
# ==========================
set(CHAOXI_NET_SOURCES
    net/Acceptor.cpp
    net/Buffer.cpp
    net/Channel.cpp
    net/Connector.cpp
    net/EventLoopThread.cpp
    net/EventLoopThreadPool.cpp
    net/InetAddress.cpp
    net/Poller.cpp

    net/poller/PollPoller.cpp
    net/poller/DefaultPoller.cpp

    net/Socket.cpp
    net/SocketOps.cpp
    net/TcpConnection.cpp
    net/TcpServer.cpp
    net/Timer.cpp
    net/TcpClient.cpp
    net/EventLoop.cpp
    net/TimerQueue.cpp

    net/http/HttpResponse.cpp
    net/http/HttpServer.cpp
    net/http/HttpContext.cpp

    # net/TimeQueue.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND CHAOXI_NET_SOURCES net/poller/EPollPoller.cpp)
endif()

# add_library(chaoxi_net STATIC ${CHAOXI_NET_SOURCES})
add_library(chaoxi_net STATIC)
add_library(chaoxi::net ALIAS chaoxi_net)
chaoxi_target_enable_warnings(chaoxi_net)
set_target_properties(
    chaoxi_net
    PROPERTIES
    EXPORT_NAME net
)
target_sources(
    chaoxi_net

    PRIVATE
        ${CHAOXI_NET_SOURCES}

    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS

        BASE_DIRS
            "${PROJECT_SOURCE_DIR}"

        FILES
            net/Acceptor.hpp
            net/Buffer.hpp
            net/Callbacks.hpp
            net/Channel.hpp
            net/Connector.hpp
            net/Endian.hpp
            net/EventLoop.hpp
            net/EventLoopThread.hpp
            net/EventLoopThreadPool.hpp
            net/InetAddress.hpp
            net/Platform.hpp
            net/Poller.hpp
            net/Socket.hpp
            net/SocketOps.hpp
            net/TcpClient.hpp
            net/TcpConnection.hpp
            net/TcpServer.hpp
            net/Timer.hpp
            net/TimerId.hpp
            net/TimerQueue.hpp

            net/http/HttpContext.hpp
            net/http/HttpRequest.hpp
            net/http/HttpResponse.hpp
            net/http/HttpServer.hpp

            net/poller/EPollPoller.hpp
            net/poller/PollPoller.hpp

    # PRIVATE
    #     FILE_SET internal_headers
    #     TYPE HEADERS
    #     BASE_DIRS "${PROJECT_SOURCE_DIR}"
    #     FILES
    #         net/poller/EPollPoller.hpp
    #         net/poller/PollPoller.hpp
)

target_compile_features(
    chaoxi_net
    PUBLIC
    cxx_std_23
)
# target_include_directories(chaoxi_net
#     PUBLIC
#     $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
#     # $<INSTALL_INTERFACE:include>
# )

target_link_libraries(chaoxi_net PUBLIC chaoxi::base)

if(WIN32)
    target_link_libraries(chaoxi_net PUBLIC ws2_32)
endif()


# ==========================
# 3. 汇总模块 (方便一把梭链接)
# ==========================
# 建立一个 INTERFACE 库，使得老代码可以使用 target_link_libraries(xxx chaoxi::chaoxi)
add_library(chaoxi INTERFACE)
add_library(chaoxi::chaoxi ALIAS chaoxi)
target_link_libraries(chaoxi INTERFACE chaoxi::base chaoxi::net)
set_target_properties(
    chaoxi
    PROPERTIES
    EXPORT_NAME chaoxi
)


# ==========================
# 4. V2 (Optional)
# ==========================
if(CHAOXI_BUILD_V2)
    add_subdirectory(v2)
    target_link_libraries(chaoxi INTERFACE chaoxi::v2)
endif()
