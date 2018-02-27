include(FindProtobuf)

set(PROTO_DEF task.proto
              response.proto)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_DEF})

add_library(messages STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_include_directories(messages PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(messages protobuf)