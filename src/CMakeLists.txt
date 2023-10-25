file(GLOB JobManager_Sources
    ${CMAKE_CURRENT_LIST_DIR}/*.h
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

add_executable(JobManager ${JobManager_Sources})
target_link_libraries(JobManager PRIVATE iBoost)
target_link_libraries(JobManager PRIVATE Threads::Threads)

install(TARGETS JobManager DESTINATION ${install_binary_dir})