set(STATIONAPI_SOURCES
  Node.hpp
  NodeClient.cpp
  NodeClient.hpp
  Serialization.hpp
  StreamUtils.cpp
  StreamUtils.hpp
  StringUtils.cpp
  StringUtils.hpp
  MariaDB.hpp
)

if (MariaDB_FOUND)
  list(APPEND STATIONAPI_SOURCES MariaDB.cpp)
else()
  list(APPEND STATIONAPI_SOURCES MariaDBStub.cpp)
endif()

add_library(
  stationapi
  ${STATIONAPI_SOURCES}
)

set(STATIONAPI_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/externals/boost
    ${PROJECT_SOURCE_DIR}/externals/catch
    ${PROJECT_SOURCE_DIR}/externals/easyloggingpp
)

if (Boost_FOUND)
  list(APPEND STATIONAPI_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
endif()

if (MariaDB_FOUND)
  list(APPEND STATIONAPI_INCLUDE_DIRS ${MariaDB_INCLUDE_DIRS})
endif()

target_include_directories(stationapi PUBLIC ${STATIONAPI_INCLUDE_DIRS})

set(STATIONAPI_LIBS udplibrary ${STATIONAPI_OPTIONAL_LIBS})
if (MariaDB_FOUND)
  list(APPEND STATIONAPI_LIBS ${MariaDB_LIBRARIES})
endif()

target_link_libraries(stationapi PUBLIC ${STATIONAPI_LIBS})
