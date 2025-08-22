# The library
cmake_minimum_required(VERSION 3.10)

#
# Populate xerr
#
FetchContent_Declare(
  xerr
  GIT_REPOSITORY https://github.com/LIONant-depot/xerr.git
  GIT_TAG        main
  SOURCE_DIR     "${CMAKE_SOURCE_DIR}/dependencies/xerr"  # Adjusted to go up one level
)

FetchContent_GetProperties(xerr)
if(NOT xerr_POPULATED)
  message(STATUS "Populating xerr...")
  FetchContent_MakeAvailable(xerr)
  
  # Include xerr's CMakeLists.txt
  add_subdirectory("${CMAKE_SOURCE_DIR}/dependencies/xerr/build/dependency" "${CMAKE_CURRENT_BINARY_DIR}/xerr")
endif()


#
# Define as an interface library only if not already defined
#
if(NOT TARGET xfile)

  # Define as a proper static library
  add_library(xfile INTERFACE)

  # determine the root of our library
  if("${TARGET_PROJECT}" STREQUAL "xfile_unit_test")
    set(ROOT ".")
  else()
    set(ROOT "dependencies/xfile")

    # set the include path
    set(xfile_INCLUDE_LIST "${ROOT}")
  endif()

  # set the files required
  set(xfile_FILES_LIST
    "${ROOT}/source/xfile.h"
    "${ROOT}/source/xfile.cpp"
    "${ROOT}/README.md"
  )

  # Set global properties
  set_property(GLOBAL PROPERTY xfile_GROUP "dependencies/xcore/xfile")
  set_property(GLOBAL PROPERTY xfile_FILES "${xfile_FILES_LIST}")
  set_property(GLOBAL PROPERTY xfile_INCLUDES "${xfile_INCLUDE_LIST}")
  set_property(GLOBAL APPEND PROPERTY COMPONENT_REGISTRY "xfile")

endif()