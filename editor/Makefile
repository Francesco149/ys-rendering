# editor/Makefile — Ys Map & Mesh Viewer
IMGUI ?= $(IMGUI_DIR)
LUA   ?= $(LUA_SRC)

BUILD := ../build
OBJ   := $(BUILD)/obj-linux
OUT   := $(BUILD)/ys-viewer

# Windows cross (mingw)
WIN_OUT := $(BUILD)/ys-viewer.exe
WIN_OBJ := $(BUILD)/obj-win

SRC := src/main.cpp src/ig.cpp src/app_paths.cpp src/falcom_archive.cpp src/dds_loader.cpp src/ymo_loader.cpp src/yco_loader.cpp src/sob_loader.cpp src/async_loader.cpp
TINYFD_C := src/vendor/tinyfiledialogs/tinyfiledialogs.c

IMGUI_CORE := imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp imgui_stdlib.cpp
RLIMGUI_SRC := src/vendor/rlImGui/rlImGui.cpp

LUA_C := $(filter-out $(LUA)/src/lua.c $(LUA)/src/luac.c,$(wildcard $(LUA)/src/*.c))

RAYLIB_CFLAGS := -I$(RAYLIB_INC)
RAYLIB_LIBS := -L$(RAYLIB_LIB) -lraylib -lz -lm -ldl -lpthread -lGL \
  -Wl,-rpath,$(RAYLIB_LIB)
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-unknown-pragmas \
  -I src -I $(BUILD) -I $(IMGUI) -I $(IMGUI)/misc/cpp \
  -I $(LUA)/src \
  -I src/vendor/rlImGui \
  -I src/vendor/tinyfiledialogs \
  $(RAYLIB_CFLAGS)

OBJ_CXX := $(addprefix $(OBJ)/,$(notdir $(SRC:.cpp=.o)) \
             $(IMGUI_CORE:.cpp=.o) rlImGui.o tinyfiledialogs.o)
OBJ_LUA := $(addprefix $(OBJ)/lua/,$(notdir $(LUA_C:.c=.o)))

linux: $(OUT)
$(OUT): $(OBJ_CXX) $(OBJ_LUA) | $(BUILD)
	$(CXX) $(OBJ_CXX) $(OBJ_LUA) -o $(OUT) -static-libgcc -static-libstdc++ $(RAYLIB_LIBS)
	@echo "built $(OUT)"

$(OBJ)/%.o: src/%.cpp src/editor.h | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/tinyfiledialogs.o: $(TINYFD_C) | $(OBJ)
	$(CC) -O2 -std=gnu11 -I src/vendor/tinyfiledialogs -c $< -o $@
$(OBJ)/%.o: $(IMGUI)/%.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/%.o: $(IMGUI)/misc/cpp/%.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/rlImGui.o: src/vendor/rlImGui/rlImGui.cpp | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@
$(OBJ)/lua/%.o: $(LUA)/src/%.c | $(OBJ)/lua
	$(CC) -O2 -std=c11 -I $(LUA)/src -c $< -o $@

# ── Windows cross (mingw) ────────────────────────────────────────────────────
WIN_CXX := $(MINGW_CXX)
WIN_CC   := $(MINGW_CC)

WIN_CXXFLAGS := $(CXXFLAGS) -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE \
  -I $(RAYLIB_CROSS_INC) -I $(ZLIB_CROSS_INC)
WIN_LDFLAGS := -static-libgcc -static-libstdc++
WIN_LIBS := -L$(RAYLIB_CROSS_LIB) -L$(ZLIB_CROSS_LIB) -lraylib -lz \
  -lopengl32 -lgdi32 -lwinmm -luser32 -lshell32 -lcomdlg32 \
  -Wl,-Bstatic -lmcfgthread -Wl,-Bdynamic \
  -ldwmapi -lole32 -lsetupapi

WIN_OBJ_CXX := $(addprefix $(WIN_OBJ)/,$(notdir $(SRC:.cpp=.o)) \
                 $(IMGUI_CORE:.cpp=.o) rlImGui.o tinyfiledialogs.o winclip.o)
WIN_OBJ_LUA := $(addprefix $(WIN_OBJ)/lua/,$(notdir $(LUA_C:.c=.o)))

win: package

$(WIN_OUT): $(WIN_OBJ_CXX) $(WIN_OBJ_LUA) | $(BUILD)
	$(WIN_CXX) $(WIN_OBJ_CXX) $(WIN_OBJ_LUA) -o $(WIN_OUT) $(WIN_LDFLAGS) $(WIN_LIBS)
	@echo "built $(WIN_OUT)"

$(WIN_OBJ)/%.o: src/%.cpp src/editor.h | $(WIN_OBJ)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@
$(WIN_OBJ)/tinyfiledialogs.o: $(TINYFD_C) | $(WIN_OBJ)
	$(WIN_CC) -O2 -std=gnu11 -I src/vendor/tinyfiledialogs -c $< -o $@
$(WIN_OBJ)/winclip.o: src/winclip.c | $(WIN_OBJ)
	$(WIN_CC) -O2 -std=c11 -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE -c $< -o $@
$(WIN_OBJ)/%.o: $(IMGUI)/%.cpp | $(WIN_OBJ)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@
$(WIN_OBJ)/%.o: $(IMGUI)/misc/cpp/%.cpp | $(WIN_OBJ)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@
$(WIN_OBJ)/rlImGui.o: src/vendor/rlImGui/rlImGui.cpp | $(WIN_OBJ)
	$(WIN_CXX) $(WIN_CXXFLAGS) -c $< -o $@
$(WIN_OBJ)/lua/%.o: $(LUA)/src/%.c | $(WIN_OBJ)/lua
	$(WIN_CC) -O2 -std=c11 -I $(LUA)/src -c $< -o $@

# Standalone folder: exe + raylib DLL + glfw DLL + mcfgthread DLL + lua/ + tests/ + assets/
package: $(WIN_OUT)
	mkdir -p $(BUILD)/ys-viewer-win64
	rm -f $(BUILD)/ys-viewer-win64/libraylib.dll $(BUILD)/ys-viewer-win64/glfw3.dll $(BUILD)/ys-viewer-win64/libmcfgthread-2.dll
	cp $(WIN_OUT) $(BUILD)/ys-viewer-win64/
	cp $(RAYLIB_CROSS_DLL) $(BUILD)/ys-viewer-win64/
	cp $(GLFW_CROSS_DLL) $(BUILD)/ys-viewer-win64/
	cp $(MCFG_DLL) $(BUILD)/ys-viewer-win64/
	rm -rf $(BUILD)/ys-viewer-win64/lua $(BUILD)/ys-viewer-win64/tests $(BUILD)/ys-viewer-win64/assets
	cp -r ../editor/lua $(BUILD)/ys-viewer-win64/lua
	cp -r ../editor/tests $(BUILD)/ys-viewer-win64/tests
	mkdir -p $(BUILD)/ys-viewer-win64/assets/fonts
	cp $(FONT_LATIN) $(BUILD)/ys-viewer-win64/assets/fonts/InterVariable.ttf
	cp $(FONT_CJK) $(BUILD)/ys-viewer-win64/assets/fonts/ipag.ttf
	@echo "packaged $(BUILD)/ys-viewer-win64/"

package-linux: $(OUT)
	mkdir -p $(BUILD)/ys-viewer-linux
	cp $(OUT) $(BUILD)/ys-viewer-linux/
	rm -rf $(BUILD)/ys-viewer-linux/lua $(BUILD)/ys-viewer-linux/tests $(BUILD)/ys-viewer-linux/assets
	cp -r ../editor/lua $(BUILD)/ys-viewer-linux/lua
	cp -r ../editor/tests $(BUILD)/ys-viewer-linux/tests
	mkdir -p $(BUILD)/ys-viewer-linux/assets/fonts
	cp $(FONT_LATIN) $(BUILD)/ys-viewer-linux/assets/fonts/InterVariable.ttf
	cp $(FONT_CJK) $(BUILD)/ys-viewer-linux/assets/fonts/ipag.ttf
	@echo "packaged $(BUILD)/ys-viewer-linux/"

$(BUILD):
	mkdir -p $(BUILD)
$(OBJ):
	mkdir -p $(OBJ)
$(OBJ)/lua:
	mkdir -p $(OBJ)/lua
$(WIN_OBJ):
	mkdir -p $(WIN_OBJ)
$(WIN_OBJ)/lua:
	mkdir -p $(WIN_OBJ)/lua

run: linux
	cd .. && ./build/ys-viewer
shot: linux
	cd .. && xvfb-run -s "-screen 0 1280x800x24" ./build/ys-viewer --shot build/shot.png --frames 20

shot-drive: linux
	cd .. && xvfb-run -s "-screen 0 1280x800x24" ./build/ys-viewer --shot build/shot_drive.png --frames 45 --drive editor/tests/drive_smoke.lua
test: linux
	cd .. && ./build/ys-viewer --test
	python3 ../src/tools/test_viewer_oracle.py
clean:
	rm -rf $(OBJ) $(WIN_OBJ) $(OUT) $(WIN_OUT) $(BUILD)/ys-viewer-win64 $(BUILD)/ys-viewer-linux

.PHONY: linux win package package-linux clean run shot shot-drive test
