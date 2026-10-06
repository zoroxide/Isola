add_executable(isola-demo demo/main.cpp)
target_link_libraries(isola-demo PRIVATE isola::isola)
set_target_properties(isola-demo PROPERTIES CXX_EXTENSIONS OFF)
