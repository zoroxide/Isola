# build with the target appropriate to the current platform:
#   make win
#   make linux
#   make run
#   make clean

CXX ?= g++
CXXFLAGS = -std=c++17 -O2 -Wall -I./Engine -I./Engine/gui -I./Engine/libs/imgui -I./Engine/libs/imgui/backends

SRC = main.cpp \
      Engine/Village.cpp \
      Engine/Sponza.cpp \
      Engine/TriangleBvh.cpp \
      Engine/Engine.cpp \
      Engine/Camera.cpp \
      Engine/Shaders.cpp \
      Engine/ECS.cpp \
      Engine/Terrain.cpp \
      Engine/Foliage.cpp \
      Engine/Textures.cpp \
      Engine/SunShadow.cpp \
      Engine/PostProcess.cpp \
      Engine/GpuProfile.cpp \
      Engine/Skybox.cpp \
      Engine/Models.cpp \
      Engine/Renderer.cpp \
      Engine/gui/gui.cpp \
      Engine/libs/stb_image.cpp \
      Engine/libs/imgui/imgui.cpp \
      Engine/libs/imgui/imgui_draw.cpp \
      Engine/libs/imgui/imgui_tables.cpp \
      Engine/libs/imgui/imgui_widgets.cpp \
      Engine/libs/imgui/backends/imgui_impl_glfw.cpp \
      Engine/libs/imgui/backends/imgui_impl_opengl3.cpp

LINUX_LIBS = -lGLEW -lglfw -lGL -ldl -lpthread -lm -lassimp -Wl,--copy-dt-needed-entries
# These names match the MinGW/MSYS2 packages: mingw-w64-*-glew, glfw and assimp.
WINDOWS_LIBS = -lglew32 -lglfw3 -lopengl32 -lgdi32 -luser32 -lkernel32 -lassimp

BUILD_DIR = build
WINDOWS_OUT = $(BUILD_DIR)/windows/program.exe
LINUX_OUT = $(BUILD_DIR)/linux/program
WINDOWS_RUN = .\build\windows\program.exe

ifeq ($(OS),Windows_NT)
HOST_OUT = $(WINDOWS_OUT)
# Make may be launched from PowerShell with MSYS2's sh.exe absent from PATH.
# Explicitly use cmd.exe for Windows recipes in that case.
SHELL := cmd.exe
.SHELLFLAGS := /C
UCRT64_BIN ?= C:/msys64/ucrt64/bin
else
HOST_OUT = $(LINUX_OUT)
endif

ifeq ($(OS),Windows_NT)
define make_dir
if not exist "$(1)" mkdir "$(1)"
endef
define remove_dir
if exist "$(1)" rmdir /S /Q "$(1)"
endef
else
define make_dir
mkdir -p "$(1)"
endef
define remove_dir
rm -rf "$(1)"
endef
endif

win: $(WINDOWS_OUT)

windows: win

$(WINDOWS_OUT): $(SRC)
	$(call make_dir,$(dir $@))
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(WINDOWS_LIBS)

linux: $(LINUX_OUT)

$(LINUX_OUT): $(SRC)
	$(call make_dir,$(dir $@))
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LINUX_LIBS)

# Run the executable built for this host. It is built first when necessary.
run: $(HOST_OUT)
ifeq ($(OS),Windows_NT)
	set "PATH=$(UCRT64_BIN);%PATH%" && $(WINDOWS_RUN)
else
	./$(HOST_OUT)
endif

clean:
	$(call remove_dir,$(BUILD_DIR))

.PHONY: win windows linux run clean
