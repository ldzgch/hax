# Build/test/lint entry points for humans, editors (nvim :make), and coding
# agents. Delegates to scripts/check.sh, which keeps successful output compact
# and keeps failure output focused on diagnostics.

BUILD_DIR ?= build

.PHONY: all tests lint install symlink clean

all:
	@scripts/check.sh build --build-dir "$(BUILD_DIR)"

tests:
	@scripts/check.sh test --build-dir "$(BUILD_DIR)"

lint:
	@scripts/check.sh lint --build-dir "$(BUILD_DIR)"

install:
	@scripts/check.sh install --build-dir "$(BUILD_DIR)"

# hax resolves subagent `hax` invocations through PATH, so development is nicest
# with the dev binary linked there; the symlink tracks every rebuild.
symlink: all
	@mkdir -p "$(HOME)/.local/bin"; \
	target="$$(cd "$(BUILD_DIR)" && pwd)/hax"; \
	link="$(HOME)/.local/bin/hax"; \
	ln -sf "$$target" "$$link" && echo "$$link -> $$target"

clean:
	rm -rf $(BUILD_DIR)
