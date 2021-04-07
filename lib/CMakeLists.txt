if (NOT TARGET fmt)
  add_library(fmt INTERFACE) # v5.3.0
  target_include_directories(fmt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fmt)
  target_compile_definitions(fmt INTERFACE FMT_HEADER_ONLY)
endif()

if (NOT TARGET aw)
  add_library(aw INTERFACE)
  target_include_directories(aw INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/aw)
endif()

if (NOT TARGET any_invocable)
  add_library(any_invocable INTERFACE)
  target_include_directories(any_invocable INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/any_invocable)
endif()
