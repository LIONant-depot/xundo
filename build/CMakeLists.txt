# D:/LIONant/LearnCMake/dependencies/xundo/build/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

include(FetchContent)

#
# Handle xcmdline
#
FetchContent_Declare(
  xcmdline
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmdline.git
  GIT_TAG        main
  SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../dependency/xcmdline"
)

FetchContent_GetProperties(xcmdline)
if(NOT xcmdline_POPULATED)
  message(STATUS "Making xcmdline available for xundo...")
  FetchContent_MakeAvailable(xcmdline)
  # Include xcmdline's configuration from the fetched location
  add_subdirectory("${xcmdline_SOURCE_DIR}/build" "${CMAKE_CURRENT_BINARY_DIR}/xcmdline")  
endif()

#
# Handle xundo
#
FetchContent_GetProperties(xundo)
if(NOT TARGET xundo)
  add_library(xundo INTERFACE)
  target_include_directories(xundo INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../source"  # Adjusted to use CMAKE_CURRENT_SOURCE_DIR for xundo
  )
  
  # Make xcmdline a dependency of xundo... this way the user just need to link with xundo...
  target_link_libraries(xundo INTERFACE xcmdline)  
endif()

