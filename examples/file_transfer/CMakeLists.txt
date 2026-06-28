add_executable(file_transfer_download download.cpp)
target_link_libraries(file_transfer_download PRIVATE chaoxi::chaoxi)

add_executable(file_transfer_download_2 download_2.cpp)
target_link_libraries(file_transfer_download_2 PRIVATE chaoxi::chaoxi)

add_executable(file_transfer_download_3 download_3.cpp)
target_link_libraries(file_transfer_download_3 PRIVATE chaoxi::chaoxi)
