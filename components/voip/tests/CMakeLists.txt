cmake_minimum_required(VERSION 3.10)
project(voip_md5_test)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MBEDTLS REQUIRED mbedtls)

include_directories(${MBEDTLS_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/..)
link_directories(${MBEDTLS_LIBRARY_DIRS})

add_executable(test_md5 test_md5.cpp)

target_link_libraries(test_md5 ${MBEDTLS_LIBRARIES})
