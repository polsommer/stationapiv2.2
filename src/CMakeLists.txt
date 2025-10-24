add_subdirectory(stationapi)

if (HAVE_BOOST_PROGRAM_OPTIONS)
  add_subdirectory(stationchat)
else()
  message(STATUS "Skipping stationchat executable because Boost program_options is unavailable.")
endif()
