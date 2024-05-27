cmake_minimum_required(VERSION 3.5)

set(PROJECT_NAME SimpleTextEditor)
project(${PROJECT_NAME})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall -Werror -DNDEBUG")
endif()

aux_source_directory(${PROJECT_SOURCE_DIR}/src SRC_LIST)
set(SOURCE_LIST Main.cpp ${SRC_LIST})

set(INCLUDE_LIST ${PROJECT_SOURCE_DIR}/include)

add_executable(${PROJECT_NAME} ${SOURCE_LIST})
