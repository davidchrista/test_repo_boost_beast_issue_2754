### general settings
cmake_minimum_required(VERSION 3.5.1)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/CMakeScripts")
cmake_policy(SET CMP0074 NEW) # consider {package}_ROOT variables for finding packages

### project settings
project(ForwardTest)
set(CMAKE_CXX_STANDARD 17)

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

MESSAGE("Boost_ROOT env variable: $ENV{Boost_ROOT}")

find_package(Boost 1.77.0 REQUIRED COMPONENTS program_options system filesystem atomic program_options chrono coroutine regex thread)

add_library(iBoost INTERFACE)
target_include_directories(iBoost SYSTEM INTERFACE "${Boost_INCLUDE_DIRS}")
target_compile_definitions(iBoost INTERFACE "_WINSOCK_DEPRECATED_NO_WARNINGS=1")
target_link_libraries(iBoost INTERFACE ${Boost_SYSTEM_LIBRARY} ${Boost_FILESYSTEM_LIBRARY})

MESSAGE(STATUS "Boost_SYSTEM_LIBRARY: ${Boost_SYSTEM_LIBRARY}")
MESSAGE(STATUS "Boost_FILESYSTEM_LIBRARY: ${Boost_FILESYSTEM_LIBRARY}")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(src)