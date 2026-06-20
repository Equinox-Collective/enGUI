CC = x86_64-elf-gcc
CXX = x86_64-elf-g++
LD = x86_64-elf-ld

SDK_DIR = ../../sdk
SDK_OBJS = $(wildcard $(SDK_DIR)/lib/*.o)

# Добавлены обязательные флаги: -fno-exceptions -fno-rtti -fno-threadsafe-statics
CXXFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
           -fno-omit-frame-pointer -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -I$(SDK_DIR)/include -I./imgui -O2 -std=c++17 -MMD -MP

CFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
         -fno-omit-frame-pointer -I$(SDK_DIR)/include -I./imgui -O2 -MMD -MP

LDFLAGS = -nostdlib -Ttext=0x1000000 -e _start

IMGUI_SRCS = $(wildcard imgui/imgui*.cpp)
GUI_SRCS = $(wildcard gui/*.cpp)
SRCS = main.cpp api_gui.cpp $(IMGUI_SRCS) $(GUI_SRCS)

OBJS = $(SRCS:.cpp=.o)

all: sysgui.elf

sysgui.elf: $(OBJS) $(SDK_OBJS)
	$(LD) $(LDFLAGS) $(SDK_OBJS) $(OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

ifeq ($(OS),Windows_NT)
    RM = del /f /q
    CLEAN_FILES = $(subst /,\,$(OBJS) $(OBJS:.o=.d) sysgui.elf)
else
    RM = rm -f
    CLEAN_FILES = $(OBJS) $(OBJS:.o=.d) sysgui.elf
endif

clean:
	-$(RM) $(CLEAN_FILES)