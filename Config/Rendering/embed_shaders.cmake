# Generates a C++ source embedding .qsb shader packages.
#   OUTPUT: The path of the source to generate.
#   HEADER: The header declaring the lookup function.
#   FUNCTION: The qualified name of the lookup function.
#   PACKAGES: The package paths separated by '|', each named
#     <shader source file name>.qsb.
string(REPLACE "|" ";" packages "${PACKAGES}")
set(arrays "")
set(lookups "")
set(index 0)
foreach(package IN LISTS packages)
  get_filename_component(file "${package}" NAME)
  string(REGEX REPLACE "\\.qsb$" "" name "${file}")
  file(READ "${package}" contents HEX)
  string(LENGTH "${contents}" length)
  if(length EQUAL 0)
    message(FATAL_ERROR "The shader package ${package} is empty.")
  endif()
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${contents}")
  string(REGEX REPLACE "((0x..,){16})" "\\1\n    " bytes "${bytes}")
  string(APPEND arrays
    "  const unsigned char PACKAGE_${index}[] = {\n    ${bytes}};\n\n")
  string(APPEND lookups "  if(name == \"${name}\") {\n"
    "    return QByteArrayView(PACKAGE_${index}, sizeof(PACKAGE_${index}));\n"
    "  }\n")
  math(EXPR index "${index} + 1")
endforeach()
set(source "#include <string_view>\n#include <QByteArrayView>\n")
string(APPEND source "#include \"${HEADER}\"\n\nnamespace {\n${arrays}}\n\n")
string(APPEND source
  "QByteArrayView ${FUNCTION}(std::string_view name) {\n${lookups}"
  "  return QByteArrayView();\n}\n")
file(WRITE "${OUTPUT}" "${source}")
