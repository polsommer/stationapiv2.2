include_directories(${PROJECT_SOURCE_DIR}/externals/catch
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/stationapi)

add_executable(stationapi_tests
    main.cpp

    stationapi/Serialization_Tests.cpp
    stationapi/StringUtils_Tests.cpp
    stationchat/ChatSystem_Tests.cpp)

target_link_libraries(stationapi_tests
    stationapi)

add_executable(stationchat_tests
    main.cpp

    stationchat/ChatRoomService_Tests.cpp
    stationchat/StubChatAvatarService.cpp
    stationchat/FakeMariaDB.cpp

    ${PROJECT_SOURCE_DIR}/src/stationchat/ChatRoomService.cpp
    ${PROJECT_SOURCE_DIR}/src/stationchat/ChatRoom.cpp
    ${PROJECT_SOURCE_DIR}/src/stationchat/ChatAvatar.cpp
    ${PROJECT_SOURCE_DIR}/src/stationchat/ChatEnums.cpp
    ${PROJECT_SOURCE_DIR}/src/stationapi/StringUtils.cpp
    ${PROJECT_SOURCE_DIR}/src/stationapi/StreamUtils.cpp)

target_include_directories(stationchat_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/externals/catch
    ${PROJECT_SOURCE_DIR}/externals/easyloggingpp)
