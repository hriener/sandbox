if (NOT TARGET fmt)
  add_library(fmt INTERFACE) # v5.3.0
  target_include_directories(fmt INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fmt)
  target_compile_definitions(fmt INTERFACE FMT_HEADER_ONLY)
endif()

if (NOT TARGET atomic_wait)
  add_library(atomic_wait INTERFACE)
  target_include_directories(atomic_wait INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/atomic_wait)
endif()

if (NOT TARGET any_invocable)
  add_library(any_invocable INTERFACE)
  target_include_directories(any_invocable INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/any_invocable)
endif()

if (NOT TARGET mockturtle)
  add_library(mockturtle INTERFACE)
  target_include_directories(mockturtle INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/mockturtle)
endif()

if (NOT TARGET lorina)
  add_library(lorina INTERFACE)
  target_include_directories(lorina INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/lorina)
endif()

if (NOT TARGET parallel_hashmap)
  add_library(parallel_hashmap INTERFACE) # 2020.11
  target_include_directories(parallel_hashmap SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/parallel_hashmap)
endif()
