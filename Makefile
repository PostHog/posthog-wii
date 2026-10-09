CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Icore

CORE := core/vff.c core/playlog.c core/ticks.c core/sha1.c core/events.c core/summary.c
HEADERS := $(wildcard core/*.h)

# libogc 3 does not link with the wii-curl packages, so the toolchain image is pinned.
DEVKIT_IMAGE := devkitpro/devkitppc:20251231
WII_SETTINGS := $(foreach name,HOST API_KEY UTC_OFFSET_MINUTES DEBUG_HOST,$(if $($(name)),$(name)="$($(name))"))

HEDGEHOG_MODE_VERSION := 0.0.62
SPRITES := assets/hedgehog-mode/sprites.png

DOLPHIN := /Applications/Dolphin.app/Contents/MacOS/Dolphin
DOLPHIN_USER := $(CURDIR)/.dolphin
DOLPHIN_BOARD := $(DOLPHIN_USER)/Wii/title/00000001/00000002/data/cdb.vff

build/wii2posthog: host/main.c host/send_curl.c $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) $(filter %.c,$^) -lcurl -o $@

build/test_core: tests/test_core.c tests/check.h $(CORE) $(HEADERS) | build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(filter %.c,$^) -o $@

build/test_game: tests/test_game.c tests/check.h game/runner.c game/runner.h | build
	$(CC) $(CFLAGS) -Igame -g -fsanitize=address,undefined $(filter %.c,$^) -o $@

build:
	mkdir -p build

$(SPRITES):
	mkdir -p $(dir $@)
	curl -sSfL https://registry.npmjs.org/@posthog/hedgehog-mode/-/hedgehog-mode-$(HEDGEHOG_MODE_VERSION).tgz \
		| tar -xz -C $(dir $@) --strip-components=2 package/assets/sprites.png package/assets/sprites.json

build/gen/atlas.png: tools/atlas.py $(SPRITES)
	uv run tools/atlas.py $(dir $(SPRITES)) build/gen

# Seeded once. Dolphin's Wii Menu writes to this file afterwards.
$(DOLPHIN_BOARD):
	mkdir -p $(dir $@)
	cp samples/cdb.vff $@

.PHONY: test wii dolphin shots clean
test: build/test_core build/test_game
	./build/test_core
	./build/test_game

wii: build/gen/atlas.png
	docker run --rm -v "$(CURDIR)":/src -w /src/wii $(DEVKIT_IMAGE) make $(WII_SETTINGS)

dolphin: wii $(DOLPHIN_BOARD)
	$(DOLPHIN) -u $(DOLPHIN_USER) -C Dolphin.Analytics.PermissionAsked=True \
		-C Graphics.Settings.InternalResolution=3 -e build/wii/boot.elf

shots: $(DOLPHIN_BOARD)
	$(MAKE) wii DEBUG_HOST=http://127.0.0.1:18010
	bash tools/shots.sh $(DOLPHIN) $(DOLPHIN_USER) 7

clean:
	rm -rf build
