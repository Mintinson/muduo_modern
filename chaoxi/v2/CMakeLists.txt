add_library(chaoxi_v2 STATIC
    AsyncAcceptor.cpp
    AsyncFd.cpp
    AsyncSocket.cpp
    Sleep.cpp
    TcpServer.cpp
)
add_library(chaoxi::v2 ALIAS chaoxi_v2)

chaoxi_target_enable_warnings(chaoxi_v2)

target_include_directories(chaoxi_v2
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(chaoxi_v2 PUBLIC chaoxi::net)
target_compile_definitions(chaoxi_v2 PUBLIC CHAOXI_HAS_V2=1)
