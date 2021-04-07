add_library(sandbox INTERFACE)
target_include_directories(sandbox INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(sandbox INTERFACE fmt aw any_invocable)
