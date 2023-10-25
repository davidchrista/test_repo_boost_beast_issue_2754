### general settings
cmake_minimum_required(VERSION 3.5.1)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeScripts")
cmake_policy(SET CMP0074 NEW) # consider {package}_ROOT variables for finding packages

# Set this environment variable to build the application with the plugin system enabled.
# If not set, the plugin libraries will be statically linked.
# The reason we provide the possiblity to link statically is the problems we have when using
# boost.asio io_contexts over ABI boundaries. See google for details about this problem.
# When using plugins with Linux, we set BOOST_ASIO_DISABLE_EPOLL to fix the problem. Note
# that this comes with a performance penalty.
#set(ENV{JOBMANAGER_BUILD_DSOS} "true")

### project settings
project(JobManager)
set(CMAKE_CXX_STANDARD 17)

option(BUILD_DOC "Build documentation" OFF)

if(CMAKE_BUILD_TYPE MATCHES "Release")
    if(NOT MSVC)
        set(JM_COMPILE_OPTIONS -O2)
    endif()
elseif(CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")
    if(NOT MSVC)
        set(JM_COMPILE_OPTIONS -O2 -g)
    endif()
else()
    if(NOT MSVC)
        set(JM_COMPILE_OPTIONS -O0 -g)
    endif()
endif()

add_compile_options(${JM_COMPILE_OPTIONS})

if(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /bigobj")
    add_compile_definitions(_WIN32_WINNT=_WIN32_WINNT_WIN10)
endif()

### dependencies
set(ENV{Boost_ROOT} $ENV{BOOST_ROOT})
set(ENV{glog_DIR} $ENV{GLOG_DIR})

MESSAGE("GTEST_ROOT env variable: $ENV{GTEST_ROOT}")
MESSAGE("Boost_ROOT env variable: $ENV{Boost_ROOT}")
MESSAGE("glog_DIR env variable: $ENV{glog_DIR}")
MESSAGE("OPENSSL_ROOT_DIR env variable: $ENV{OPENSSL_ROOT_DIR}")

find_package(Boost 1.77.0 REQUIRED COMPONENTS program_options system filesystem atomic program_options chrono coroutine regex thread)

add_definitions(-DBOOST_ERROR_CODE_HEADER_ONLY)
add_definitions(-DBOOST_ALL_NO_LIB=1)
if(DEFINED ENV{JOBMANAGER_BUILD_DSOS})
    add_definitions(-DJOBMANAGER_BUILD_DSOS)
    if (NOT MSVC)
        add_definitions(-DBOOST_ASIO_DISABLE_EPOLL)
    endif()
endif()

add_library(iBoost INTERFACE)
target_include_directories(iBoost SYSTEM INTERFACE "${Boost_INCLUDE_DIRS}")
target_compile_definitions(iBoost INTERFACE "_WINSOCK_DEPRECATED_NO_WARNINGS=1")
target_link_libraries(iBoost INTERFACE ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY})

MESSAGE(STATUS "Boost_SYSTEM_LIBRARY: ${Boost_SYSTEM_LIBRARY}")
MESSAGE(STATUS "Boost_FILESYSTEM_LIBRARY: ${Boost_FILESYSTEM_LIBRARY}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(glog 0.6.0 REQUIRED)

find_package(OpenSSL REQUIRED)

### installation configuration
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/deploy" CACHE PATH "Install to deploy directory by default" FORCE)
endif()

set(install_binary_dir ${CMAKE_INSTALL_PREFIX})
set(install_library_dir ${CMAKE_INSTALL_PREFIX})

# documentation

if (BUILD_DOC)
    find_package(Doxygen)
    
    set(DOXYGEN_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/docs)
    set(DOXYFILE_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYFILE_OUT ${CMAKE_CURRENT_BINARY_DIR}/docs/Doxyfile)

    # create Doxyfile by substitution of CMake variables in Doxyfile.in
    configure_file(${DOXYFILE_IN} ${DOXYFILE_OUT} @ONLY)

    # note the option ALL which allows to build the docs together with the application
    add_custom_target(docs ALL
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYFILE_OUT}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/docs
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM )
endif (BUILD_DOC)

add_subdirectory(src)

install(FILES $<TARGET_FILE:Boost::system> DESTINATION ${install_library_dir})
install(FILES $<TARGET_FILE:Boost::filesystem> DESTINATION ${install_library_dir})
install(FILES $<TARGET_FILE:Boost::atomic> DESTINATION ${install_library_dir})
install(FILES $<TARGET_FILE:Boost::program_options> DESTINATION ${install_library_dir})
install(FILES "start.sh" DESTINATION ${install_library_dir})