file(GLOB_RECURSE library_sources "*.cpp" "*.c")
idf_component_register(SRCS ${library_sources} INCLUDE_DIRS ".")
