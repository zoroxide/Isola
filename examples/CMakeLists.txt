add_executable(nut-demo demo/main.cpp)
target_link_libraries(nut-demo PRIVATE nut::nut)
set_target_properties(nut-demo PROPERTIES CXX_EXTENSIONS OFF)
