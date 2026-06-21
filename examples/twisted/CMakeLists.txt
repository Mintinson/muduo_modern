# add_executable(finger_01 finger/finger_01.cpp)
# target_link_libraries(finger_01 PRIVATE chaoxi::chaoxi)

# add_executable(finger_02 finger/finger_02.cpp)
# target_link_libraries(finger_02 PRIVATE chaoxi::chaoxi)

# add_executable(finger_03 finger/finger_03.cpp)
# target_link_libraries(finger_03 PRIVATE chaoxi::chaoxi)

function(add_finger_executable TARGET_NAME)
    add_executable(twisted_${TARGET_NAME} "finger/${TARGET_NAME}.cpp" ${ARGN})
    target_link_libraries(twisted_${TARGET_NAME} PRIVATE chaoxi::chaoxi)
endfunction()


add_finger_executable(finger_01)
add_finger_executable(finger_02)
add_finger_executable(finger_03)
add_finger_executable(finger_04)
add_finger_executable(finger_05)
add_finger_executable(finger_06)
add_finger_executable(finger_07)