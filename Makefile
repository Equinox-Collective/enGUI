CC = x86_64-elf-gcc
CXX = x86_64-elf-g++
LD = x86_64-elf-ld

SDK_DIR = ../../sdk
SDK_LIB = $(SDK_DIR)/lib/libequos.a
LIBC = ../../third_party/musl/lib/libc.a

LVGL_DIR = ../../third_party/lvgl
LVGL_LIB = $(LVGL_DIR)/liblvgl.a

CXXFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
           -fno-omit-frame-pointer -fno-exceptions -fno-rtti -fno-threadsafe-statics \
           -I../../third_party/musl/include -I$(SDK_DIR)/include -O2 -std=c++17 -MMD -MP \
           -I$(LVGL_DIR) -I. -DLV_CONF_INCLUDE_SIMPLE

CFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
         -fno-omit-frame-pointer -I../../third_party/musl/include -I$(SDK_DIR)/include -O2 -MMD -MP \
         -I$(LVGL_DIR) -I. -DLV_CONF_INCLUDE_SIMPLE

LDFLAGS = -nostdlib -T app.ld

GUI_SRCS = $(wildcard gui/*.cpp) $(wildcard gui/apps/*.cpp)
SRCS = main.cpp api_gui.cpp $(GUI_SRCS)

OBJS = $(SRCS:.cpp=.o)

all: sysgui.elf

sysgui.elf: $(OBJS) $(SDK_LIB) $(LVGL_LIB)
	$(LD) $(LDFLAGS) $(OBJS) $(SDK_LIB) $(LVGL_LIB) $(LIBC) -o $@

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