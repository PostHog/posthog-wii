ifeq ($(strip $(DEVKITPPC))$(strip $(MBEDTLS)),$(strip $(DEVKITPPC)))
$(error DEVKITPPC or MBEDTLS is not set. Run "make wii" from the repository root)
endif

include $(DEVKITPPC)/wii_rules

# Where the debug build posts its frames.
DEBUG_HOST ?=

OUT := ../build/wii
GEN := ../build/gen
# Both builds link the game and the HTTP client with everything under it.
SHARED := readonly.c http.c tls.c noise.c sdcard.c gfx.c play.c ../game/runner.c $(wildcard ../core/*.c) \
	$(GEN)/hedgehog_tpl.s
HEADERS := $(wildcard *.h ../core/*.h ../game/*.h) $(GEN)/atlas.h roots.pem
GUARD := ../tools/nand_guard.sh

# mbedTLS has a mbedtls_config.h of its own, so the file here needs another name.
TLS_FLAGS := -I$(MBEDTLS)/include -I. '-DMBEDTLS_USER_CONFIG_FILE="tls_config.h"'
TLS_LIB := $(OUT)/libmbedtls.a
TLS_OBJECTS := $(patsubst $(MBEDTLS)/library/%.c,$(OUT)/mbedtls/%.o,$(wildcard $(MBEDTLS)/library/*.c))

CFLAGS := -std=gnu11 -g -O2 -Wall -Wextra -Werror $(MACHDEP) -I../core -I../game -I$(GEN) -I$(LIBOGC_INC) $(TLS_FLAGS)
# See readonly.c.
WRAP := -Wl,--wrap=IOS_Write -Wl,--wrap=IOS_WriteAsync
LIBS := $(WRAP) -L$(LIBOGC_LIB) -lfat -lwiiuse -lbte -logc -lm

$(OUT)/boot.dol: $(OUT)/boot.elf
	elf2dol $< $@

# The app syncs the play history, then runs the game.
$(OUT)/boot.elf: main.c nand.c $(SHARED) $(TLS_LIB) $(HEADERS) $(GUARD) Makefile
	$(CC) $(CFLAGS) $(filter %.c %.s %.a,$^) $(LIBS) -o $@
	bash $(GUARD) $@ || { rm -f $@; exit 1; }

# The debug build runs the game alone, playing by itself. See debug.c.
$(OUT)/debug.elf: debug.c ../game/autoplay.c $(SHARED) $(TLS_LIB) $(HEADERS) $(GUARD) Makefile
	$(CC) $(CFLAGS) -DDEBUG_HOST='"$(DEBUG_HOST)"' $(filter %.c %.s %.a,$^) $(LIBS) -o $@
	bash $(GUARD) $@ || { rm -f $@; exit 1; }

$(TLS_LIB): $(TLS_OBJECTS)
	rm -f $@
	$(AR) rcs $@ $^

$(OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c tls_config.h Makefile | $(OUT)/mbedtls
	$(CC) -O2 $(MACHDEP) -ffunction-sections -fdata-sections $(TLS_FLAGS) -c $< -o $@

$(OUT)/mbedtls:
	mkdir -p $@

# gxtexconv also writes hedgehog.h, a list of texture IDs that nothing reads.
$(GEN)/hedgehog.tpl: $(GEN)/atlas.png
	gxtexconv -i $< -o $@ colfmt=6

$(GEN)/hedgehog_tpl.s: $(GEN)/hedgehog.tpl
	cd $(GEN) && bin2s -a 32 -H hedgehog_tpl.h hedgehog.tpl > hedgehog_tpl.s
