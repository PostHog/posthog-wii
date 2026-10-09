ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set. Run "make wii" from the repository root)
endif

include $(DEVKITPPC)/wii_rules

# Where the debug build posts its frames.
DEBUG_HOST ?=

OUT := ../build/wii
GEN := ../build/gen
GAME := readonly.c http.c gfx.c play.c ../game/runner.c $(GEN)/hedgehog_tpl.s
HEADERS := $(wildcard *.h ../core/*.h ../game/*.h) $(GEN)/atlas.h
GUARD := ../tools/nand_guard.sh

CFLAGS := -std=gnu11 -g -O2 -Wall -Wextra -Werror $(MACHDEP) -I../core -I../game -I$(GEN) -I$(LIBOGC_INC)
# See readonly.c.
WRAP := -Wl,--wrap=IOS_Write -Wl,--wrap=IOS_WriteAsync
LIBS := $(WRAP) -L$(LIBOGC_LIB) -lfat -lwiiuse -lbte -logc -lm

$(OUT)/boot.dol: $(OUT)/boot.elf
	elf2dol $< $@

# The app: the play history sync, then the game.
$(OUT)/boot.elf: main.c nand.c sdcard.c $(GAME) $(wildcard ../core/*.c) $(HEADERS) $(GUARD) Makefile | $(OUT)
	$(CC) $(CFLAGS) $(filter %.c %.s,$^) $(LIBS) -o $@
	bash $(GUARD) $@ || { rm -f $@; exit 1; }

# The debug build: the game alone, playing by itself. See debug.c.
$(OUT)/debug.elf: debug.c ../game/autoplay.c $(GAME) $(HEADERS) $(GUARD) Makefile | $(OUT)
	$(CC) $(CFLAGS) -DDEBUG_HOST='"$(DEBUG_HOST)"' $(filter %.c %.s,$^) $(LIBS) -o $@
	bash $(GUARD) $@ || { rm -f $@; exit 1; }

$(OUT):
	mkdir -p $@

# gxtexconv also writes hedgehog.h, a list of texture IDs that nothing reads.
$(GEN)/hedgehog.tpl: $(GEN)/atlas.png
	gxtexconv -i $< -o $@ colfmt=6

$(GEN)/hedgehog_tpl.s: $(GEN)/hedgehog.tpl
	cd $(GEN) && bin2s -a 32 -H hedgehog_tpl.h hedgehog.tpl > hedgehog_tpl.s
