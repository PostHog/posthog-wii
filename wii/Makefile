ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC is not set. Run "make wii" from the repository root)
endif

include $(DEVKITPPC)/wii_rules

HOST ?= http://127.0.0.1:8010
API_KEY ?=
UTC_OFFSET_MINUTES ?= 0
# Set to a listener URL to build the scripted, self-capturing debug game.
DEBUG_HOST ?=

OUT := ../build/wii
GEN := ../build/gen
SOURCES := main.c nand.c http.c gfx.c play.c ../game/runner.c \
	../core/vff.c ../core/playlog.c ../core/ticks.c ../core/sha1.c ../core/events.c ../core/summary.c

CFLAGS := -std=gnu11 -g -O2 -Wall -Wextra -Werror $(MACHDEP) -I../core -I../game -I$(GEN) -I$(OUT) -I$(LIBOGC_INC)
LIBS := -L$(LIBOGC_LIB) -lwiiuse -lbte -logc -lm

define SETTINGS_H
#define POSTHOG_HOST "$(HOST)"
#define POSTHOG_API_KEY "$(API_KEY)"
#define UTC_OFFSET_MINUTES ($(UTC_OFFSET_MINUTES))
$(if $(DEBUG_HOST),#define DEBUG_HOST "$(DEBUG_HOST)")
endef
export SETTINGS_H

$(OUT)/boot.dol: $(OUT)/boot.elf
	elf2dol $< $@

$(OUT)/boot.elf: $(SOURCES) $(GEN)/hedgehog_tpl.s $(wildcard *.h ../core/*.h ../game/*.h) $(GEN)/atlas.h $(OUT)/settings.h Makefile
	$(CC) $(CFLAGS) $(filter %.c %.s,$^) $(LIBS) -o $@

# The file is rewritten only when a setting changes, so that alone rebuilds.
$(OUT)/settings.h: FORCE
	@mkdir -p $(OUT)
	@printf '%s\n' "$$SETTINGS_H" | cmp -s - $@ || printf '%s\n' "$$SETTINGS_H" > $@

# gxtexconv also writes hedgehog.h, a list of texture IDs that nothing reads.
$(GEN)/hedgehog.tpl: $(GEN)/atlas.png
	gxtexconv -i $< -o $@ colfmt=6

$(GEN)/hedgehog_tpl.s: $(GEN)/hedgehog.tpl
	cd $(GEN) && bin2s -a 32 -H hedgehog_tpl.h hedgehog.tpl > hedgehog_tpl.s

.PHONY: FORCE
FORCE:
