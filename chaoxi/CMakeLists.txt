# ==========================
# 1. Base 模块 (底层的工具、日志、线程池等)
# ==========================
# TODO: 当项目完成的时候，将下面两句解开，方便一点
# file(GLOB_RECURSE BASE_SOURCES CONFIGURE_DEPENDS "base/*.cpp")
# add_library(chaoxi_base STATIC ${BASE_SOURCES})
add_library(chaoxi_base STATIC 
    base/Logging.cpp
    base/LogStream.cpp
    base/CurrentThread.cpp
)
add_library(chaoxi::base ALIAS chaoxi_base)

target_include_directories(chaoxi_base
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# 基础库可能用到了线程
find_package(Threads REQUIRED)
target_link_libraries(chaoxi_base PUBLIC Threads::Threads stdc++exp)



# ==========================
# 2. Net 模块 (网络核心层)
# ==========================
# file(GLOB_RECURSE NET_SOURCES CONFIGURE_DEPENDS "net/*.cpp")
# add_library(chaoxi_net STATIC ${NET_SOURCES})
add_library(chaoxi_net STATIC 
    net/InetAddress.cpp
    net/SocketOps.cpp
    net/Buffer.cpp
    net/Timer.cpp
    net/Channel.cpp
    net/EventLoop.cpp
    net/Poller.cpp
    net/TimerQueue.cpp
    net/poller/DefaultPoller.cpp
    net/poller/PollPoller.cpp
    net/poller/EPollPoller.cpp
    net/EventLoopThread.cpp
    net/EventLoopThreadPool.cpp
    net/Socket.cpp
    net/Acceptor.cpp
    net/TcpConnection.cpp
    net/Connector.cpp
    net/TcpServer.cpp
    net/TcpClient.cpp

    # net/TimeQueue.cpp
)
add_library(chaoxi::net ALIAS chaoxi_net)

target_include_directories(chaoxi_net
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# 网络层往往重度依赖基础层，将依赖通过 PUBLIC 透传
target_link_libraries(chaoxi_net PUBLIC chaoxi::base)


# ==========================
# 3. 汇总模块 (方便一把梭链接)
# ==========================
# 建立一个 INTERFACE 库，使得老代码可以使用 target_link_libraries(xxx chaoxi::chaoxi)
add_library(chaoxi INTERFACE)
add_library(chaoxi::chaoxi ALIAS chaoxi)
target_link_libraries(chaoxi INTERFACE chaoxi::base chaoxi::net)
