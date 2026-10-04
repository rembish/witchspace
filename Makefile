# Everyday commands. The game builds with CMake alone; this only gathers the usual steps.
# `make help` lists them. The differential tests need your copy of Elite Plus in original/.

BUILD     ?= build
BUILD_WEB ?= build-web
EMSDK_ENV ?= $(HOME)/tools/emsdk/emsdk_env.sh
UV        ?= uv
C_FILES    = core/*.c core/*.h src/*.c src/*.h tests/*.c

VERSION   := $(shell cat VERSION)

.PHONY: help version-check build web run test clips format format-check data-check difftest py-sync py-format py-check check

help: ## This list
	@grep -E '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | awk -F':.*## ' '{printf "  %-13s %s\n", $$1, $$2}'

version-check: ## The version is the same in VERSION, pyproject.toml and CHANGELOG.md
	@test "$$(sed -n 's/^version = "\(.*\)"/\1/p' pyproject.toml)" = "$(VERSION)" || \
		{ echo "pyproject.toml's version is not $(VERSION)"; exit 1; }
	@grep -q '^## \[$(VERSION)\]' CHANGELOG.md || { echo "CHANGELOG.md has no section for $(VERSION)"; exit 1; }
	@echo "version $(VERSION)"

build: ## The game and the test tools (warnings are errors)
	cmake -S . -B $(BUILD) -DWS_WERROR=ON
	cmake --build $(BUILD) -j

web: ## The browser version (Emscripten: build-web/witchspace.html)
	. $(EMSDK_ENV) >/dev/null && emcmake cmake -S . -B $(BUILD_WEB) -DCMAKE_BUILD_TYPE=Release
	. $(EMSDK_ENV) >/dev/null && cmake --build $(BUILD_WEB) -j --target witchspace

run: build ## Play, with the game's files from original/
	./$(BUILD)/witchspace --data original

test: build ## The checks outside the emulator (skipped without original/)
	ctest --test-dir $(BUILD) --output-on-failure

CLIPS ?= clips
clips: build ## Short videos of scripted scenes into clips/ (from original/; needs ffmpeg)
	mkdir -p $(CLIPS)
	./$(BUILD)/ep_clips original $(CLIPS)
	for s in title launch screens docking hyperspace; do \
		ffmpeg -loglevel error -y -f rawvideo -pix_fmt rgb24 -s 320x200 -r 30 -i $(CLIPS)/$$s.rgb \
			-i $(CLIPS)/$$s.wav -vf scale=960:720:flags=neighbor -c:v libx264 -pix_fmt yuv420p -crf 20 \
			-c:a aac -b:a 128k -shortest -movflags +faststart $(CLIPS)/witchspace-$$s.mp4 && \
		rm $(CLIPS)/$$s.rgb $(CLIPS)/$$s.wav; done

format: ## Format the C code (clang-format 21) and the Python (ruff)
	clang-format -i $(C_FILES)
	$(UV) run ruff format re

format-check: ## Check formatting only
	clang-format --dry-run --Werror $(C_FILES)
	$(UV) run ruff format --check re

data-check: build ## The core's loader against an independent extraction of the tables
	$(UV) run re/tools/gen_tables.py --check $(BUILD)/ep_datadump

difftest: build ## Every reconstructed routine against the original (about an hour)
	cd re/emu && for t in $$($(UV) run subtest.py --list); do \
		$(UV) run subtest.py $$t --fuzz 2 --show 2 | tail -1; done

py-sync: ## The Python environment (.venv) for the tools in re/
	$(UV) sync --extra dev

py-format: ## Format the Python
	$(UV) run ruff format re

py-check: ## Python: format check, lint, strict types
	$(UV) run ruff format --check re
	$(UV) run ruff check re
	$(UV) run mypy

check: version-check format-check py-check test data-check ## What CI checks, plus the data check
