CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Icore -Igame

CORE := $(wildcard core/*.c)
HEADERS := $(wildcard core/*.h game/*.h tests/*.h)

# Fetched, not committed. The hash is the one the mbedTLS release publishes.
MBEDTLS_VERSION := 3.6.7
MBEDTLS_SHA256 := a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
MBEDTLS := assets/mbedtls-$(MBEDTLS_VERSION)

# A new image brings a new libogc. See the Safety section of CLAUDE.md.
DEVKIT_IMAGE := devkitpro/devkitppc:20251231
DEVKIT := docker run --rm -v "$(CURDIR)":/src -w /src/wii $(DEVKIT_IMAGE) make -j8 MBEDTLS=../$(MBEDTLS)

# The pictures come from PostHog's own npm packages. They are fetched, not
# committed. See the Brand section of CLAUDE.md.
HEDGEHOG_MODE_VERSION := 0.0.62
BRAND_VERSION := 0.12.3
SPRITES := assets/hedgehog-mode/sprites.png
CRESTS := assets/brand/crests
ICON_ART := assets/brand/hoggies/two-player.png

# The folder to copy to the top of an SD card.
SD := build/sd/apps/posthog-wii

DOLPHIN := /Applications/Dolphin.app/Contents/MacOS/Dolphin
DOLPHIN_USER := $(CURDIR)/.dolphin
DOLPHIN_BOARD := $(DOLPHIN_USER)/Wii/title/00000001/00000002/data/cdb.vff
# Dolphin packs this folder into its SD card when a run starts.
DOLPHIN_SETTINGS := $(DOLPHIN_USER)/Load/WiiSDSync/apps/posthog-wii/settings.txt
SHOTS_PORT := 18010

# What "make dolphin" and "make sd" write to the settings file.
HOST ?= http://localhost:8010
API_KEY ?=
UTC_OFFSET ?= +00:00
write_settings = printf 'host = %s\napi_key = %s\nutc_offset = %s\n' '$(HOST)' '$(API_KEY)' '$(UTC_OFFSET)' > $(1)
# The random seed that an https send needs. mbedTLS reads 64 bytes of it. See wii/tls_config.h.
write_seed = head -c 64 /dev/urandom > $(1)

build/wii2posthog: host/main.c host/http.c $(CORE) $(HEADERS) host/http.h | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -lcurl -o $@

build/test_core: tests/test_core.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(filter %.c,$^) -o $@

build/test_game: tests/test_game.c game/runner.c game/autoplay.c $(HEADERS) | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(filter %.c,$^) -o $@

build:
	mkdir -p build

$(SPRITES):
	mkdir -p $(dir $@)
	curl -sSfL https://registry.npmjs.org/@posthog/hedgehog-mode/-/hedgehog-mode-$(HEDGEHOG_MODE_VERSION).tgz \
		| tar -xz -C $(dir $@) --strip-components=2 package/assets/sprites.png package/assets/sprites.json

$(CRESTS):
	mkdir -p $@
	curl -sSfL https://registry.npmjs.org/@posthog/brand/-/brand-$(BRAND_VERSION).tgz \
		| tar -xz -C $@ --strip-components=6 'package/dist/generated/crests/mini/png/*.png'

$(ICON_ART):
	mkdir -p $(dir $@)
	curl -sSfL https://registry.npmjs.org/@posthog/brand/-/brand-$(BRAND_VERSION).tgz \
		| tar -xz -C $(dir $@) --strip-components=5 package/dist/generated/hoggies/png/$(notdir $@)

build/gen/atlas.png: tools/atlas.py $(SPRITES) | $(CRESTS)
	uv run tools/atlas.py $(dir $(SPRITES)) $(CRESTS) build/gen

$(SD)/icon.png: tools/icon.py $(ICON_ART)
	uv run tools/icon.py $(ICON_ART) $@

$(MBEDTLS):
	mkdir -p assets
	curl -sSfL -o assets/mbedtls.tar.bz2 \
		https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-$(MBEDTLS_VERSION)/mbedtls-$(MBEDTLS_VERSION).tar.bz2
	echo "$(MBEDTLS_SHA256)  assets/mbedtls.tar.bz2" | shasum -a 256 -c -
	tar -xjf assets/mbedtls.tar.bz2 -C assets
	rm assets/mbedtls.tar.bz2

# Seeded once. Dolphin's Wii Menu writes to this file afterwards.
$(DOLPHIN_BOARD):
	mkdir -p $(dir $@)
	cp samples/cdb.vff $@

.PHONY: test wii sd dolphin shots clean
test: build/test_core build/test_game
	./build/test_core
	./build/test_game

wii: build/gen/atlas.png | $(MBEDTLS)
	$(DEVKIT)

# A console cannot reach the default HOST, which is this computer's own address.
sd: wii $(SD)/icon.png
	@test '$(origin HOST)' != file -a -n '$(API_KEY)' || { echo 'sd: pass HOST, API_KEY and UTC_OFFSET' >&2; exit 1; }
	cp build/wii/boot.dol wii/meta.xml $(SD)/
	$(call write_settings,$(SD)/settings.txt)
	$(call write_seed,$(SD)/seed.bin)

dolphin: wii $(DOLPHIN_BOARD)
	mkdir -p $(dir $(DOLPHIN_SETTINGS))
	$(call write_settings,$(DOLPHIN_SETTINGS))
	$(call write_seed,$(dir $(DOLPHIN_SETTINGS))seed.bin)
	$(DOLPHIN) -u $(DOLPHIN_USER) -C Dolphin.Analytics.PermissionAsked=True \
		-C Dolphin.Core.WiiSDCardEnableFolderSync=True \
		-C Graphics.Settings.InternalResolution=3 -e build/wii/boot.elf

shots: build/gen/atlas.png | $(MBEDTLS)
	$(DEVKIT) ../build/wii/debug.elf DEBUG_HOST=http://127.0.0.1:$(SHOTS_PORT)
	bash tools/shots.sh $(DOLPHIN) $(DOLPHIN_USER) $(SHOTS_PORT)

clean:
	rm -rf build
