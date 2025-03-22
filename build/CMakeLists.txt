# D:/LIONant/LearnCMake/dependencies/xundo/build/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

# Include FetchContent module
include(FetchContent)

# Declare xcmdline with custom download directory under xundo
FetchContent_Declare(
  xcmdline
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmdline.git
  GIT_TAG        main
  SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../dependency/xcmdline"
)

# Populate xcmdline only if not already populated
FetchContent_MakeAvailable(xcmdline)
if(NOT xcmdline_POPULATED)
  message(STATUS "Populating xcmdline for xundo...")
  FetchContent_Populate(xcmdline)
  if(NOT EXISTS "${xcmdline_SOURCE_DIR}/source/xcmdline_parser.h")
    message(FATAL_ERROR "Failed to download xcmdline or xcmdline_parser.h not found in ${xcmdline_SOURCE_DIR}")
  endif()
endif()

# Define xcmdline as an interface library only if not already defined
if(NOT TARGET xcmdline)
  add_subdirectory("../dependency/xcmdline/build" "${CMAKE_CURRENT_BINARY_DIR}/xcmdline")
  add_library(xcmdline INTERFACE)
  target_include_directories(xcmdline INTERFACE "${xcmdline_SOURCE_DIR}/source")
endif()

# Define xundo as an interface library only if not already defined
if(NOT TARGET xundo)
  add_library(xundo INTERFACE)
  # Assuming xundo has headers in its root; adjust if different
  target_include_directories(xundo INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../source")
  target_link_libraries(xundo INTERFACE xcmdline)
endif()