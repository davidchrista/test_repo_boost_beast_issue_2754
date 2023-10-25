file(GLOB ForwardTest_Sources
    ${CMAKE_CURRENT_LIST_DIR}/*.h
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_executable(ForwardTest ${ForwardTest_Sources})
target_link_libraries(ForwardTest PRIVATE iBoost)
target_link_libraries(ForwardTest PRIVATE Threads::Threads)