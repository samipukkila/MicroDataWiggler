cmake_minimum_required(VERSION 3.14)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" MICRODATAWIGGLER_VERSION LIMIT_COUNT 1)

project(
  Microdatawiggler
  VERSION ${MICRODATAWIGGLER_VERSION}
  DESCRIPTION "Record and modify microcontroller variables through STlink"
  LANGUAGES CXX C)

message("Building with CMake version: ${CMAKE_VERSION}")

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

find_package(GTest REQUIRED)
find_package(fff REQUIRED)

include(GoogleTest)
enable_testing()

function(add_unit_test)
  set(options OPTIONAL)
  set(oneValueArgs TEST_NAME)
  set(multiValueArgs SRCS HDRS LIBS)
  cmake_parse_arguments(PARSE_ARGV 0 arg
      "${options}" "${oneValueArgs}" "${multiValueArgs}"
  )

  add_executable(${arg_TEST_NAME}
    ${arg_SRCS}
  )

  target_include_directories(${arg_TEST_NAME} PUBLIC
    ${arg_HDRS}
  )

  target_link_libraries(
    ${arg_TEST_NAME}
    GTest::gtest_main
    fff::fff
    ${arg_LIBS}
  )

  gtest_discover_tests(${arg_TEST_NAME}
    PROPERTIES LABELS "unit"
  )
endfunction()

function(add_integration_test)
  set(options OPTIONAL)
  set(oneValueArgs TEST_NAME)
  set(multiValueArgs SRCS HDRS LIBS)
  cmake_parse_arguments(PARSE_ARGV 0 arg
      "${options}" "${oneValueArgs}" "${multiValueArgs}"
  )

  add_executable(${arg_TEST_NAME}
    ${arg_SRCS}
  )

  target_include_directories(${arg_TEST_NAME} PUBLIC
    ${arg_HDRS}
  )

  target_link_libraries(
    ${arg_TEST_NAME}
    GTest::gtest_main
    ${arg_LIBS}
  )

  gtest_discover_tests(${arg_TEST_NAME}
    PROPERTIES LABELS "integration"
  )
endfunction()

set(CPACK_PACKAGE_NAME "microdatawiggler")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Sami Pukkila <sami.pukkila@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libstlink1, libusb-1.0-0, gdb-multiarch, libgrpc++1.51t64")
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS OFF)
set(CPACK_PACKAGE_DESCRIPTION "Record and modify microcontroller variables through STlink")
set(CMAKE_INSTALL_PREFIX "/usr")

include(CPack)

add_subdirectory(microdatawiggler)

