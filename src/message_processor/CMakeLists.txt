# Message Processor Module
target_sources(app PRIVATE
  message_processor.c
)

target_include_directories(app PRIVATE 
  ${CMAKE_CURRENT_SOURCE_DIR}
)