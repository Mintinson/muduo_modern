add_executable(roundtrip roundtrip.cpp)
target_link_libraries(roundtrip chaoxi::chaoxi)

add_executable(roundtrip_udp roundtrip_udp.cpp)
target_link_libraries(roundtrip_udp chaoxi::chaoxi)