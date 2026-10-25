# Wii U build (devkitPro + wut).  Produces kruczek.rpx and kruczek.wuhb (Aroma).
#   dkp-pacman -S wiiu-dev       (once)
#   make
ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO (e.g. export DEVKITPRO=/opt/devkitpro)")
endif
include $(DEVKITPRO)/wut/share/wut_rules

TARGET   := kruczek
SRCS     := src/game.c src/gfx.c src/audio.c src/platform_wiiu.c
OBJS     := $(SRCS:.c=.o)
PORTLIBS := $(DEVKITPRO)/portlibs/wiiu $(DEVKITPRO)/portlibs/ppc

CC       := powerpc-eabi-gcc
CFLAGS   := -O3 -ffast-math -ffunction-sections -fno-strict-aliasing -Wall $(MACHDEP) \
            -I$(WUT_ROOT)/include $(foreach d,$(PORTLIBS),-I$(d)/include) -D__WIIU__ -D__WUT__
LDFLAGS  := $(ARCH) $(RPXSPECS) -Wl,-Map,$(TARGET).map
LIBDIRS  := -L$(WUT_ROOT)/lib $(foreach d,$(PORTLIBS),-L$(d)/lib)
LIBS     := -lSDL2 -lwut -lm

all: $(TARGET).wuhb

$(TARGET).wuhb: $(TARGET).rpx
	wuhbtool $< $@ --name="Kruczek Survival" --short-name="Kruczek" --author="Luk" \
	  --icon=assets/icon.png --tv-image=assets/tv.png --drc-image=assets/drc.png

$(TARGET).elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBDIRS) $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).rpx $(TARGET).wuhb $(TARGET).map

.PHONY: all clean
