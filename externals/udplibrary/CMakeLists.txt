add_library(
  udplibrary
  STATIC
  UdpLibrary.cpp
)

target_include_directories(
  udplibrary
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
