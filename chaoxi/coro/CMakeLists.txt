# add_library(chaoxi_v2 STATIC
#     AsyncAcceptor.cpp
#     AsyncFd.cpp
#     AsyncSocket.cpp
#     Sleep.cpp
#     TcpServer.cpp
# )
add_library(chaoxi_coro STATIC)
add_library(chaoxi::coro ALIAS chaoxi_coro)

chaoxi_target_enable_warnings(chaoxi_coro)

target_sources(
    chaoxi_coro

    PRIVATE
        AsyncAcceptor.cpp
        AsyncFd.cpp
        AsyncSocket.cpp
        Sleep.cpp
        TcpServer.cpp

    PUBLIC
        FILE_SET HEADERS
        TYPE HEADERS

        BASE_DIRS
            "${PROJECT_SOURCE_DIR}"

        FILES
            AsyncAcceptor.hpp
            AsyncFd.hpp
            AsyncSocket.hpp
            Chaoxi.hpp
            Scheduler.hpp
            Sleep.hpp
            Spawn.hpp
            Task.hpp
            TcpServer.hpp
)

set_target_properties(
    chaoxi_coro
    PROPERTIES
        EXPORT_NAME coro
)

target_compile_features(
    chaoxi_coro
    PUBLIC
        cxx_std_23
)
target_include_directories(chaoxi_coro
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(chaoxi_coro PUBLIC chaoxi::net)
target_compile_definitions(chaoxi_coro PUBLIC CHAOXI_HAS_CORO=1)
