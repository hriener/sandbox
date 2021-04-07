if (NOT TARGET fmt)
  add_library(fmt INTERFACE) # v5.3.0
  target_include_directories(fmt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fmt)
  target_compile_definitions(fmt INTERFACE FMT_HEADER_ONLY)
endif()
