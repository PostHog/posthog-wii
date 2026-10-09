CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Icore -Igame

CORE := $(wildcard core/*.c)
HEADERS := $(wildcard core/*.h game/*.h tests/*.h)

# libogc 3 does not link with the wii-curl packages, so the toolchain image is pinned.
DEVKIT_IMAGE := devkitpro/devkitppc:20251231
DEVKIT := docker run --rm -v "$(CURDIR)":/src -w /src/wii $(DEVKIT_IMAGE) make

# The pictures come from PostHog's own npm packages. They are fetched, not
# committed: see the Brand section of CLAUDE.md.
HEDGEHOG_MODE_VERSION := 0.0.62
BRAND_VERSION := 0.12.3
SPRITES := assets/hedgehog-mode/sprites.png
CRESTS := assets/brand/crests

DOLPHIN := /Applications/Dolphin.app/Contents/MacOS/Dolphin
DOLPHIN_USER := $(CURDIR)/.dolphin
DOLPHIN_BOARD := $(DOLPHIN_USER)/Wii/title/00000001/00000002/data/cdb.vff
# Dolphin packs this folder into its SD card when a run starts.
DOLPHIN_SETTINGS := $(DOLPHIN_USER)/Load/WiiSDSync/apps/posthog-wii/settings.txt
SHOTS_PORT := 18010

# What "make dolphin" writes to the settings file on Dolphin's SD card.
HOST ?= http://127.0.0.1:8010
API_KEY ?=
UTC_OFFSET ?= +00:00

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

build/gen/atlas.png: tools/atlas.py $(SPRITES) | $(CRESTS)
	uv run tools/atlas.py $(dir $(SPRITES)) $(CRESTS) build/gen

# Seeded once. Dolphin's Wii Menu writes to this file afterwards.
$(DOLPHIN_BOARD):
	mkdir -p $(dir $@)
	cp samples/cdb.vff $@

.PHONY: test wii dolphin shots clean
test: build/test_core build/test_game
	./build/test_core
	./build/test_game

wii: build/gen/atlas.png
	$(DEVKIT)

dolphin: wii $(DOLPHIN_BOARD)
	mkdir -p $(dir $(DOLPHIN_SETTINGS))
	printf 'host = %s\napi_key = %s\nutc_offset = %s\n' '$(HOST)' '$(API_KEY)' '$(UTC_OFFSET)' > $(DOLPHIN_SETTINGS)
	$(DOLPHIN) -u $(DOLPHIN_USER) -C Dolphin.Analytics.PermissionAsked=True \
		-C Dolphin.Core.WiiSDCardEnableFolderSync=True \
		-C Graphics.Settings.InternalResolution=3 -e build/wii/boot.elf

shots: build/gen/atlas.png
	$(DEVKIT) ../build/wii/debug.elf DEBUG_HOST=http://127.0.0.1:$(SHOTS_PORT)
	bash tools/shots.sh $(DOLPHIN) $(DOLPHIN_USER) $(SHOTS_PORT)

clean:
	rm -rf build
