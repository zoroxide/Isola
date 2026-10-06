cmake_minimum_required(VERSION 3.16)

# The version lives in include/isola/Version.hpp
file(STRINGS include/isola/Version.hpp _isola_version REGEX "#define ISOLA_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*ISOLA_VERSION_${_part} ([0-9]+).*" "\\1" ISOLA_VERSION_${_part} "${_isola_version}")
endforeach()

project(Isola
    VERSION ${ISOLA_VERSION_MAJOR}.${ISOLA_VERSION_MINOR}.${ISOLA_VERSION_PATCH}
    DESCRIPTION "OpenGL game engine: procedural islands, the Sponza palace and a first-person player"
    LANGUAGES CXX)

include(GNUInstallDirs)

set(ISOLA_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(ISOLA_IS_TOP_LEVEL ON)
endif()

option(ISOLA_BUILD_EXAMPLES "Build the demo application" ${ISOLA_IS_TOP_LEVEL})
option(ISOLA_INSTALL "Generate install and package-config rules" ${ISOLA_IS_TOP_LEVEL})
option(ISOLA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- Dependencies ------------------------------------------------------------------------------
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(GLEW REQUIRED)
find_package(assimp REQUIRED)
find_package(Threads REQUIRED)
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    # Older glm packages ship no CMake config: use the headers directly
    find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
    add_library(glm::glm INTERFACE IMPORTED)
    set_target_properties(glm::glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${GLM_INCLUDE_DIR}")
endif()

set(ISOLA_IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
if(NOT EXISTS ${ISOLA_IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR "Dear ImGui is missing: run `git submodule update --init`")
endif()

# --- Library -----------------------------------------------------------------------------------
add_library(isola
    src/Engine.cpp
    src/EngineCore.cpp
    src/Map.cpp
    src/Camera.cpp
    src/Foliage.cpp
    src/GpuProfile.cpp
    src/PostProcess.cpp
    src/Renderer.cpp
    src/Shaders.cpp
    src/Skybox.cpp
    src/Sponza.cpp
    src/SunShadow.cpp
    src/Terrain.cpp
    src/Textures.cpp
    src/TriangleBvh.cpp
    src/Village.cpp
    src/gui/Gui.cpp
    third_party/stb/stb_image.cpp
    ${ISOLA_IMGUI_DIR}/imgui.cpp
    ${ISOLA_IMGUI_DIR}/imgui_draw.cpp
    ${ISOLA_IMGUI_DIR}/imgui_tables.cpp
    ${ISOLA_IMGUI_DIR}/imgui_widgets.cpp
    ${ISOLA_IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${ISOLA_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
add_library(isola::isola ALIAS isola)

target_compile_features(isola PUBLIC cxx_std_17)
set_target_properties(isola PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME isola)

target_include_directories(isola
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        src
        third_party/stb
        ${ISOLA_IMGUI_DIR}
        ${ISOLA_IMGUI_DIR}/backends)

# glm types appear in the public headers; everything else is an implementation detail
target_link_libraries(isola
    PUBLIC glm::glm
    PRIVATE glfw GLEW::GLEW OpenGL::GL assimp::assimp Threads::Threads ${CMAKE_DL_LIBS})

if(BUILD_SHARED_LIBS)
    target_compile_definitions(isola PUBLIC ISOLA_SHARED PRIVATE ISOLA_BUILDING)
endif()

if(MSVC)
    target_compile_options(isola PRIVATE /W4 /permissive- $<$<BOOL:${ISOLA_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(isola PRIVATE -Wall $<$<BOOL:${ISOLA_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# --- Examples ----------------------------------------------------------------------------------
if(ISOLA_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# --- Install: headers, library, shaders, CMake package (find_package(Isola)) --------------------
if(ISOLA_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS isola EXPORT IsolaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(DIRECTORY include/isola DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY shaders/ DESTINATION ${CMAKE_INSTALL_DATADIR}/isola/shaders)

    set(ISOLA_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Isola)
    install(EXPORT IsolaTargets NAMESPACE isola:: DESTINATION ${ISOLA_CMAKE_DIR})
    configure_package_config_file(cmake/IsolaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/IsolaConfig.cmake
        INSTALL_DESTINATION ${ISOLA_CMAKE_DIR})
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/IsolaConfigVersion.cmake
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/IsolaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/IsolaConfigVersion.cmake
        DESTINATION ${ISOLA_CMAKE_DIR})
endif()
