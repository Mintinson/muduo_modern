find_package(CURL 7.60 QUIET)

if(NOT TARGET CURL::libcurl) 
    message(STATUS "[EXAMPLES] CURL not found - skipping curl example (chaoxi_curl / curl_exe)")
    return()
endif()

add_library(chaoxi_curl curl.cpp)
target_sources(chaoxi_curl PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES curl.hpp
)
target_link_libraries(chaoxi_curl
    PUBLIC chaoxi::net CURL::libcurl
)
chaoxi_target_enable_warnings(chaoxi_curl)

add_executable(curl_exe curl_exe.cpp)
target_link_libraries(curl_exe PRIVATE chaoxi_curl)
chaoxi_target_enable_warnings(curl_exe)

add_executable(curl_download download.cpp)
target_link_libraries(curl_download PRIVATE chaoxi_curl)
chaoxi_target_enable_warnings(curl_download)
