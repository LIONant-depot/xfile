include(${CMAKE_BINARY_DIR}/_deps/xcmake_tools/Common.cmake)

message(STATUS "Fetching URL: https://github.com/LIONant-depot/xerr.git")
FetchAndPopulate("https://github.com/LIONant-depot/xerr.git")

DefineInterfaceComponent(xfile "dependencies/xcore"
  "source/xfile.h"
  "source/xfile.cpp"
  "README.md"
)
