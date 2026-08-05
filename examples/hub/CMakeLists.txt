# hub 是服务端进程。它只需要协议解析器，不依赖客户端封装。
add_executable(hub hub.cpp codec.cpp)
target_link_libraries(hub PRIVATE chaoxi::chaoxi)

# pub 和 sub 共用 PubSubClient 与 wire codec，抽成静态库避免重复实现。
add_library(chaoxi_pubsub pubsub.cpp codec.cpp)
target_link_libraries(chaoxi_pubsub PUBLIC chaoxi::chaoxi)

# 两个命令行前端都只负责参数/生命周期管理，网络逻辑来自 chaoxi_pubsub。
add_executable(pub pub.cpp)
target_link_libraries(pub PRIVATE chaoxi_pubsub)

add_executable(sub sub.cpp)
target_link_libraries(sub PRIVATE chaoxi_pubsub)
