# Compiles GLSL 440 shaders into .qsb packages at build time, targeting
# SPIR-V, GLSL ES 100, GLSL 120 and 150, HLSL 5.0 and MSL 1.2, and embeds them
# into a target as a generated source defining a lookup function.
#   target: The target to embed the shaders into.
#   HEADER: The header declaring the lookup function, included by the
#     generated source.
#   FUNCTION: The qualified name of the lookup function, taking a
#     std::string_view naming a shader source file and returning a
#     QByteArrayView over its package, empty if there is none.
#   SHADERS: The shader sources, each embedded under its file name.
function(rellu_add_shaders target)
  cmake_parse_arguments(PARSE_ARGV 1 argument "" "HEADER;FUNCTION" "SHADERS")
  set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_shaders.cmake")
  set(directory "${CMAKE_CURRENT_BINARY_DIR}/${target}Shaders")
  set(packages)
  foreach(shader IN LISTS argument_SHADERS)
    get_filename_component(name "${shader}" NAME)
    set(package "${directory}/${name}.qsb")
    add_custom_command(OUTPUT "${package}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${directory}"
      COMMAND "${QT_SHADER_COMPILER_PATH}" --glsl "100 es,120,150"
        --hlsl 50 --msl 12 -o "${package}" "${shader}"
      DEPENDS "${shader}" "${QT_SHADER_COMPILER_PATH}"
      COMMENT "Compiling shader ${name}"
      VERBATIM)
    list(APPEND packages "${package}")
  endforeach()
  set(source "${directory}/${target}Shaders.cpp")
  string(REPLACE ";" "|" package_list "${packages}")
  add_custom_command(OUTPUT "${source}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${source}"
      "-DHEADER=${argument_HEADER}" "-DFUNCTION=${argument_FUNCTION}"
      "-DPACKAGES=${package_list}" -P "${script}"
    DEPENDS ${packages} "${script}"
    COMMENT "Embedding shaders into ${target}"
    VERBATIM)
  target_sources(${target} PRIVATE ${argument_SHADERS} "${source}")
  source_group("Shader Files" FILES ${argument_SHADERS})
  source_group("Generated Files" FILES "${source}")
endfunction()
