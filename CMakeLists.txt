cmake_minimum_required(VERSION 3.10)
project(xfile_unit_test LANGUAGES CXX)
set(TARGET_PROJECT "xfile_unit_test")
set(COMPONENT_REGISTRY "")

# Fetch Common.cmake from xcmake_tools repository
include(FetchContent)
FetchContent_Declare(
  xcmake_tools
  GIT_REPOSITORY https://github.com/LIONant-depot/xcmake_tools.git
  GIT_TAG main  # Track the latest commit on main branch
  GIT_SHALLOW TRUE  # Shallow clone for efficiency
  SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/xcmake_tools"
)
FetchContent_MakeAvailable(xcmake_tools)
include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

# Define the main executable
# This is important to be first so we cann add the target_links to it...
add_executable(xfile_unit_test "source/unit_tests/main.cpp"
                               "source/unit_tests/xfile_unit_test.h"
)

#
# Organize the project a little
#
source_group("unit_test" FILES "source/unit_tests/xfile_unit_test.h" 
)

source_group("" FILES "source/unit_tests/main.cpp"
)

#
# Populate requirements for xfile
#
add_subdirectory("build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xfile")

#
# Process components
#
ProcessComponents()

