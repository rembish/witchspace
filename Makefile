# Everyday commands. The game builds with CMake alone; this only gathers the usual steps.
# `make help` lists them. The differential tests need your copy of Elite Plus in original/.

BUILD     ?= build
BUILD_WEB ?= build-web
EMSDK_ENV ?= $(HOME)/tools/emsdk/emsdk_env.sh
UV        ?= uv
CMAKE     ?= cmake
BUILD_WIN ?= build-win
C_FILES    = core/*.c core/*.h src/*.c src/*.h tests/*.c

VERSION   := $(shell cat VERSION)

.PHONY: help version-check build web run test clips icon difftest difftest-run windows wintest windifftest format format-check data-check py-sync py-format py-check check

help: ## This list
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-13s %s\n", $$1, $$2}'

version-check: ## The version is the same in VERSION, pyproject.toml and CHANGELOG.md
	@test "$$(sed -n 's/^version = "\(.*\)"/\1/p' pyproject.toml)" = "$(VERSION)" || \
		{ echo "pyproject.toml's version is not $(VERSION)"; exit 1; }
	@grep -q '^## \[$(VERSION)\]' CHANGELOG.md || { echo "CHANGELOG.md has no section for $(VERSION)"; exit 1; }
	@echo "version $(VERSION)"

build: ## The game and the test tools (warnings are errors)
	$(CMAKE) -S . -B $(BUILD) -DWS_WERROR=ON
	$(CMAKE) --build $(BUILD) -j

web: SHELL := /bin/bash # emsdk_env.sh finds its folder only from bash
web: ## The browser version (Emscripten: build-web/witchspace.html)
	. $(EMSDK_ENV) >/dev/null && emcmake cmake -S . -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release
	. $(EMSDK_ENV) >/dev/null && cmake --build $(BUILD_WEB) -j --target witchspace

run: build ## Play, with the game's files from original/
	./$(BUILD)/witchspace --data original

test: build ## The checks outside the emulator (skipped without original/)
	ctest --test-dir $(BUILD) --output-on-failure

CLIPS ?= clips
# each scene: a video with sound, and a 6-second looping preview (the GIF keeps the game's own
# 256 colours exactly) starting this many seconds in
CLIP_PREVIEW = title:13 launch:1 screens:2 docking:8 hyperspace:7 combat:1
clips: build ## Short videos and previews of scripted scenes into clips/ (from original/; needs ffmpeg)
	mkdir -p $(CLIPS)
	./$(BUILD)/ep_clips original $(CLIPS)
	for p in $(CLIP_PREVIEW); do s=$${p%:*}; t=$${p#*:}; \
		ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 320x200 -r 30 -i $(CLIPS)/$$s.rgb \
			-i $(CLIPS)/$$s.wav -vf scale=960:720:flags=neighbor -c:v libx264 -pix_fmt yuv420p -crf 20 \
			-c:a aac -b:a 128k -shortest -movflags +faststart $(CLIPS)/witchspace-$$s.mp4 && \
		ffmpeg -loglevel error -y -ss $$t -t 6 -f rawvideo -pix_fmt rgb24 -s 320x200 -r 30 -i $(CLIPS)/$$s.rgb \
			-vf "fps=15,split[a][b];[a]palettegen=max_colors=256:stats_mode=full[p];[b][p]paletteuse=dither=none" \
			-loop 0 $(CLIPS)/witchspace-$$s.gif && \
		rm $(CLIPS)/$$s.rgb $(CLIPS)/$$s.wav; done

format: ## Format the C code (clang-format 21) and the Python (ruff)
	clang-format -i $(C_FILES)
	$(UV) run ruff format re tools

format-check: ## Check formatting only
	clang-format --dry-run --Werror $(C_FILES)
	$(UV) run ruff format --check re tools

data-check: build ## The core's loader against an independent extraction of the tables
	$(UV) run re/tools/gen_tables.py --check $(BUILD)/ep_datadump

difftest: build ## Every reconstructed routine against the original (about an hour); fails if any differs
	@# after the build, not beside it (make -j), and with this build's tool
	$(MAKE) --no-print-directory difftest-run SUBSYS=$(abspath $(BUILD))/ep_subsys

icon: ## The icon's sizes (Windows, the window, the web page) from assets/icon.png
	$(UV) run tools/make_icon.py

windows: ## The Windows version and test tools, cross-built with MinGW-w64 (build-win/)
	cmake -S . -B $(BUILD_WIN) -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DWS_VENDOR_SDL=ON -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_WIN) -j

wintest: windows ## From WSL: the Windows build's tests, and the game, run on Windows
	tests/wintest.sh $(BUILD_WIN)

windifftest: wintest ## From WSL: every routine against the original, with the Windows build
	$(MAKE) difftest-run SUBSYS=$(CURDIR)/$(BUILD_WIN)/ep_subsys-wsl

difftest-run: # the comparisons themselves (tests/difftest_runner.sh checks that failures fail)
	@cd re/emu || exit 1; \
	names=$$($(UV) run subtest.py --list) || { echo "difftest: could not list the routines"; exit 1; }; \
	[ -n "$$names" ] || { echo "difftest: no routines listed"; exit 1; }; \
	failed=""; for t in $$names; do \
		out=$$($(UV) run subtest.py $$t $(if $(SUBSYS),'corpus/*.bin' $(SUBSYS)) --fuzz 2 --show 2 2>&1); status=$$?; \
		echo "$$out" | tail -1; [ $$status = 0 ] || failed="$$failed $$t"; done; \
	if [ -n "$$failed" ]; then echo "difftest FAILED:$$failed"; exit 1; else echo "difftest: all passed"; fi

py-sync: ## The Python environment (.venv) for the tools in re/
	$(UV) sync --extra dev

py-format: ## Format the Python
	$(UV) run ruff format re tools

py-check: ## Python: format check, lint, strict types
	$(UV) run ruff format --check re tools
	$(UV) run ruff check re tools
	$(UV) run mypy

check: version-check format-check py-check test data-check ## What CI checks, plus the data check
