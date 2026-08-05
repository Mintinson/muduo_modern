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

# TODO: 当项目完成的时候，将下面两句解开，方便一点
# file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "base/*.cpp")
# add_library(chaoxi_base STATIC ${BASE_SOURCES})
add_library(chaoxi_base STATIC
    base/Logging.cpp
    base/LogStream.cpp
    base/CurrentThread.cpp
    base/LogFile.cpp
    base/FileUtil.cpp
    base/ProcessInfo.cpp
    base/AsyncLogging.cpp
)
add_library(chaoxi::base ALIAS chaoxi_base)
chaoxi_target_enable_warnings(chaoxi_base)

target_include_directories(chaoxi_base
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# 基础库可能用到了线程
find_package(Threads REQUIRED)
target_link_libraries(chaoxi_base PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(chaoxi_base PUBLIC stdc++exp)
endif()



# ==========================
# 2. Net 模块 (网络核心层)
# ==========================
# file(GLOB_RECURSE NET_SOURCES CONFIGURE_DEPENDS "net/*.cpp")
# add_library(chaoxi_net STATIC ${NET_SOURCES})
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
# if(WIN32 AND CHAOXI_WINDOWS_USE_WEPOLL)
#     # wepoll is a C library. Enable C only for this Windows backend so Linux
#     # users keep the project's original C++-compiler-only requirement.
#     enable_language(C)
#     include(FetchContent)
#     FetchContent_Declare(
#         wepoll
#         URL https://github.com/piscisaureus/wepoll/archive/refs/tags/v1.5.8.tar.gz
#         URL_HASH SHA256=3f68e5f2f5a35c2eb641b73a6d0e605668ea66e5bd9ff1e170660e3b2a60c74e
#         DOWNLOAD_EXTRACT_TIMESTAMP TRUE
#     )
#     FetchContent_MakeAvailable(wepoll)

#     add_library(chaoxi_wepoll STATIC ${wepoll_SOURCE_DIR}/wepoll.c)
#     target_include_directories(chaoxi_wepoll PUBLIC ${wepoll_SOURCE_DIR})
#     target_link_libraries(chaoxi_wepoll PUBLIC ws2_32)
#     list(APPEND CHAOXI_NET_SOURCES net/poller/WEPollPoller.cpp)
# endif()
add_library(chaoxi_net STATIC ${CHAOXI_NET_SOURCES})
add_library(chaoxi::net ALIAS chaoxi_net)
chaoxi_target_enable_warnings(chaoxi_net)

target_include_directories(chaoxi_net
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

# 网络层往往重度依赖基础层，将依赖通过 PUBLIC 透传
target_link_libraries(chaoxi_net PUBLIC chaoxi::base)
if(WIN32)
    target_link_libraries(chaoxi_net PUBLIC ws2_32)
    # if(CHAOXI_WINDOWS_USE_WEPOLL)
    #     target_link_libraries(chaoxi_net PRIVATE chaoxi_wepoll)
    #     target_compile_definitions(chaoxi_net PUBLIC CHAOXI_HAS_WEPOLL=1)
    # endif()
endif()


# ==========================
# 3. 汇总模块 (方便一把梭链接)
# ==========================
# 建立一个 INTERFACE 库，使得老代码可以使用 target_link_libraries(xxx chaoxi::chaoxi)
add_library(chaoxi INTERFACE)
add_library(chaoxi::chaoxi ALIAS chaoxi)
target_link_libraries(chaoxi INTERFACE chaoxi::base chaoxi::net)
