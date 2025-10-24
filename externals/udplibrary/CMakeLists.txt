add_library(udplibrary
    src/UdpLibrary.cpp)

add_library(udplibrary::udplibrary ALIAS udplibrary)

target_include_directories(
    udplibrary
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(udplibrary PUBLIC cxx_std_17)
