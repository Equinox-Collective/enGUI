CC = x86_64-elf-gcc
CXX = x86_64-elf-g++
LD = x86_64-elf-ld

SDK_DIR = ../../sdk
SDK_OBJS = $(wildcard $(SDK_DIR)/lib/*.o)

CXXFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
           -fno-omit-frame-pointer -fno-exceptions -fno-rtti -std=c++17 \
           -I$(SDK_DIR)/include -I. -O3 -MMD -MP

LDFLAGS = -nostdlib -Ttext=0x1000000 -e _start

IMGUI_SRCS = imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_widgets.cpp imgui/imgui_tables.cpp imgui/imgui_demo.cpp
GUI_SRCS = main.cpp api_gui.cpp

ALL_SRCS = $(GUI_SRCS) $(IMGUI_SRCS)
ALL_OBJS = $(ALL_SRCS:.cpp=.o)

all: sysgui.elf

sysgui.elf: $(ALL_OBJS) $(SDK_OBJS)
	$(LD) $(LDFLAGS) $(SDK_OBJS) $(ALL_OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(ALL_OBJS:.o=.d)

ifeq ($(OS),Windows_NT)
    RM = del /f /q
    CLEAN_FILES = $(subst /,\,$(ALL_OBJS) $(ALL_OBJS:.o=.d) sysgui.elf)
else
    RM = rm -f
    CLEAN_FILES = $(ALL_OBJS) $(ALL_OBJS:.o=.d) sysgui.elf
endif

clean:
	-$(RM) $(CLEAN_FILES)