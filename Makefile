CC ?= cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Icore -Igame

CORE := $(wildcard core/*.c)
HEADERS := $(wildcard core/*.h game/*.h tests/*.h)

# The build downloads mbedTLS instead of committing it. The hash is the one
# the mbedTLS release page publishes.
MBEDTLS_VERSION := 3.6.7
MBEDTLS_SHA256 := a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
MBEDTLS := assets/mbedtls-$(MBEDTLS_VERSION)

# A new image brings a new libogc. See the Safety section of CLAUDE.md. The
# digest names one image, where a tag can move to another.
DEVKIT_IMAGE := devkitpro/devkitppc:20251231@sha256:6e9c24976b1eab09ac349bf899c8a68a7dd154a30d5a3bace23ff9e846a18bab
DEVKIT := docker run --rm -v "$(CURDIR)":/src -w /src/wii $(DEVKIT_IMAGE) make -j8 MBEDTLS=../$(MBEDTLS)

# The pictures come from PostHog's own npm packages, which the build downloads
# instead of committing. See the Brand section of CLAUDE.md. Each hash is the
# version's dist.integrity in the npm registry, written in hexadecimal.
HEDGEHOG_MODE_VERSION := 0.0.62
HEDGEHOG_MODE_SHA512 := ebcacd35cc7f49bf1fdbb93e742bc34c30c67be9c7acd984bcbd4ce1285fc342d554a2267dc92a96a36e19c068c38841e151988d664c3334242ada1a54d38f43
BRAND_VERSION := 0.12.3
BRAND_SHA512 := 5729b5b7d09fe33d481911fc1ddd259f54221bc7322af6b7f28719b28e913a196d01ac18e982c9e648acbe0fd8f36773d70dbc49f601d76270f9bbd44fd3f845
# Fetches a package to $@.tgz and stops when the file does not have the hash.
# Usage: $(call fetch_npm,<package>,<version>,<hash>)
fetch_npm = curl -sSfL -o $@.tgz https://registry.npmjs.org/$(1)/-/$(notdir $(1))-$(2).tgz \
	&& echo "$(3)  $@.tgz" | shasum -a 512 -c -
SPRITES := assets/hedgehog-mode/sprites.png
CRESTS := assets/brand/crests
ICON_ART := assets/brand/hoggies/two-player.png

# .pre-commit-config.yaml pins every other formatter and linter. A new version
# of one can change what it accepts.
PRE_COMMIT := uvx pre-commit@4.7.0
CLANG_TIDY := uvx clang-tidy@23.1.4
HOST_C_SOURCES := $(wildcard core/*.c game/*.c host/*.c tests/*.c)
WII_C_SOURCES := $(wildcard wii/*.c)
# The pip build of clang-tidy does not find the macOS system headers by itself.
SDK := $(shell xcrun --show-sdk-path 2>/dev/null)
# clang-tidy and editors read each folder's compile_flags.txt. The one in wii/
# names the macros that devkitPPC's GCC defines for the Wii, and the build
# image's headers. The rule below copies those headers out of the image into
# this folder. That GCC accepts #embed in GNU C11 without a warning.
DEVKIT_HEADERS := build/devkit

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
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined $(filter %.c,$^) -lm -o $@

build:
	mkdir -p build

$(SPRITES):
	mkdir -p $(dir $@)
	$(call fetch_npm,@posthog/hedgehog-mode,$(HEDGEHOG_MODE_VERSION),$(HEDGEHOG_MODE_SHA512))
	tar -xzf $@.tgz -C $(dir $@) --strip-components=2 package/assets/sprites.png package/assets/sprites.json
	rm $@.tgz

# The rule makes the folder after the hash check. Make treats a folder that
# exists as built, so a failed download must not leave one behind.
$(CRESTS):
	mkdir -p $(dir $@)
	$(call fetch_npm,@posthog/brand,$(BRAND_VERSION),$(BRAND_SHA512))
	mkdir -p $@
	tar -xzf $@.tgz -C $@ --strip-components=6 package/dist/generated/crests/mini/png
	find $@ -type f ! -name '*.png' -delete
	rm $@.tgz

$(ICON_ART):
	mkdir -p $(dir $@)
	$(call fetch_npm,@posthog/brand,$(BRAND_VERSION),$(BRAND_SHA512))
	tar -xzf $@.tgz -C $(dir $@) --strip-components=5 package/dist/generated/hoggies/png/$(notdir $@)
	rm $@.tgz

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

# The first run copies the sample into Dolphin's NAND. Dolphin's Wii Menu
# writes to this file afterwards, so later runs keep it.
$(DOLPHIN_BOARD):
	mkdir -p $(dir $@)
	cp samples/cdb.vff $@

$(DEVKIT_HEADERS):
	mkdir -p $@.tmp
	id=$$(docker create $(DEVKIT_IMAGE)) \
		&& docker cp -q $$id:/opt/devkitpro/libogc/include $@.tmp/libogc \
		&& docker cp -q $$id:/opt/devkitpro/devkitPPC/powerpc-eabi/include $@.tmp/newlib; \
		status=$$?; docker rm $$id > /dev/null; exit $$status
	mv $@.tmp $@

.PHONY: test lint wii sd dolphin shots clean
test: build/test_core build/test_game
	./build/test_core
	./build/test_game

# The hooks fix formatting in place and then fail, so a second run passes.
# The Wii build generates a header that wii/gfx.c includes.
lint: wii $(DEVKIT_HEADERS)
	$(PRE_COMMIT) run --all-files
	ln -sfn ../../$(MBEDTLS)/include $(DEVKIT_HEADERS)/mbedtls
	$(CLANG_TIDY) --quiet $(if $(SDK),--extra-arg=-isysroot$(SDK)) $(HOST_C_SOURCES)
	$(CLANG_TIDY) --quiet $(WII_C_SOURCES)

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
