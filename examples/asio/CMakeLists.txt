add_executable(chat_client chat_client.cpp)
target_link_libraries(chat_client PRIVATE chaoxi::chaoxi)

add_executable(chat_server chat_server.cpp)
target_link_libraries(chat_server PRIVATE chaoxi::chaoxi)

add_executable(chat_server_threaded server_threaded.cpp)
target_link_libraries(chat_server_threaded PRIVATE chaoxi::chaoxi)

add_executable(chat_server_thread_local server_thread_local.cpp)
target_link_libraries(chat_server_thread_local PRIVATE chaoxi::chaoxi)


add_subdirectory(tutorial)