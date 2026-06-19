find_package(PkgConfig REQUIRED)
pkg_check_modules(STLINK REQUIRED IMPORTED_TARGET stlink)
pkg_check_modules(GRPCPP REQUIRED IMPORTED_TARGET grpc++)
pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)

find_program(PROTOC_EXECUTABLE NAMES protoc REQUIRED)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE NAMES grpc_cpp_plugin REQUIRED)

# Generate gRPC/protobuf sources from .proto file
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/microdatawiggler.proto)
set(PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

set(PROTO_SRCS ${PROTO_OUT_DIR}/microdatawiggler.pb.cc)
set(PROTO_HDRS ${PROTO_OUT_DIR}/microdatawiggler.pb.h)
set(GRPC_SRCS ${PROTO_OUT_DIR}/microdatawiggler.grpc.pb.cc)
set(GRPC_HDRS ${PROTO_OUT_DIR}/microdatawiggler.grpc.pb.h)

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
  COMMAND ${PROTOC_EXECUTABLE}
    --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
    --cpp_out=${PROTO_OUT_DIR}
    --grpc_out=${PROTO_OUT_DIR}
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
    ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating gRPC/protobuf sources from microdatawiggler.proto"
)

# Create a library target for protobuf/gRPC files
add_library(microdatawiggler_proto_lib OBJECT
  ${PROTO_SRCS}
  ${GRPC_SRCS}
)

target_include_directories(microdatawiggler_proto_lib PUBLIC
  ${PROTO_OUT_DIR}
)

target_link_libraries(microdatawiggler_proto_lib PUBLIC
  PkgConfig::GRPCPP
  PkgConfig::PROTOBUF
)

add_subdirectory(unit_tests)
add_subdirectory(integration_tests)

file(GLOB PROJECT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(microdatawiggler
  ${PROJECT_SOURCES}
  $<TARGET_OBJECTS:microdatawiggler_proto_lib>
)

target_include_directories(microdatawiggler PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/inc
  ${PROTO_OUT_DIR}
)

target_link_libraries(microdatawiggler PRIVATE
  microdatawiggler_proto_lib
  PkgConfig::STLINK
  PkgConfig::GRPCPP
  PkgConfig::PROTOBUF
)

install(TARGETS microdatawiggler RUNTIME DESTINATION bin)
