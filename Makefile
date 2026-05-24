CC = x86_64-elf-gcc
AR = x86_64-elf-ar
SDK_DIR = ../../sdk
CFLAGS = -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone \
		-I$(SDK_DIR)/include -Ilua -O2

LDFLAGS = -T $(SDK_DIR)/lib/link.ld -L$(SDK_DIR)/lib -lequos_sdk

LUA_SRCS = $(wildcard lua/l*.c)
GUI_SRCS = main.c api_gui.c

LUA_OBJS = $(LUA_SRCS:.c=.o)
GUI_OBJS = $(GUI_SRCS:.c=.o)

all: sysgui.elf

sysgui.elf: $(LUA_OBJS) $(GUI_OBJS)
	$(CC) $(LUA_OBJS) $(GUI_OBJS) -o $@ $(CFLAGS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(LUA_OBJS) $(GUI_OBJS) sysgui.elf