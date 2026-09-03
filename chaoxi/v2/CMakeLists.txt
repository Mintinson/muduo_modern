# add_library(chaoxi_v2 STATIC
#     AsyncAcceptor.cpp
#     AsyncFd.cpp
#     AsyncSocket.cpp
#     Sleep.cpp
#     TcpServer.cpp
# )
add_library(chaoxi_v2 STATIC)
add_library(chaoxi::v2 ALIAS chaoxi_v2)

chaoxi_target_enable_warnings(chaoxi_v2)

target_sources(
    chaoxi_v2

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
    chaoxi_v2
    PROPERTIES
        EXPORT_NAME v2
)

target_compile_features(
    chaoxi_v2
    PUBLIC
        cxx_std_23
)
target_include_directories(chaoxi_v2
    PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
)

target_link_libraries(chaoxi_v2 PUBLIC chaoxi::net)
target_compile_definitions(chaoxi_v2 PUBLIC CHAOXI_HAS_V2=1)
