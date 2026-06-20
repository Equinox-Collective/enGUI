CC = x86_64-elf-gcc
LD = x86_64-elf-ld

SDK_DIR = ../../sdk
SDK_OBJS = $(wildcard $(SDK_DIR)/lib/*.o)

CFLAGS = -ffreestanding -mcmodel=small -mno-red-zone -fno-stack-protector -fno-pic -g \
         -fno-omit-frame-pointer -I$(SDK_DIR)/include -Ilua -O2 -MMD -MP

LDFLAGS = -nostdlib -Ttext=0x1000000 -e _start

LUA_SRCS = $(wildcard lua/l*.c)
GUI_SRCS = main.c api_gui.c

LUA_OBJS = $(LUA_SRCS:.c=.o)
GUI_OBJS = $(GUI_SRCS:.c=.o)
ALL_OBJS = $(LUA_OBJS) $(GUI_OBJS)

all: sysgui.elf

# Добавлена явная зависимость от SDK_OBJS, чтобы sysgui перелинковывался при изменении SDK
sysgui.elf: $(ALL_OBJS) $(SDK_OBJS)
	$(LD) $(LDFLAGS) $(SDK_OBJS) $(ALL_OBJS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Автоматическое отслеживание изменений в заголовочных файлах (.h)
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