# --------------------------------------------
# Convenience Makefile for day-to-day C++ dev
# Works with CMake presets: debug, asan, ubsan, release
# --------------------------------------------

SHELL := /usr/bin/env bash

# Build dirs (must match your CMakePresets.json)
DEBUG_DIR   ?= build/debug
ASAN_DIR    ?= build/asan
UBSAN_DIR   ?= build/ubsan
RELEASE_DIR   ?= build/release
COVERAGE_DIR  ?= build/coverage

# Test binaries (adjust names if different)
TEST_BIN_DEBUG  := $(DEBUG_DIR)/tests
TEST_BIN_ASAN   := $(ASAN_DIR)/tests
TEST_BIN_UBSAN  := $(UBSAN_DIR)/tests

# Allow passing extra args, e.g. `make test GTEST_FILTER=Basic.*`
GTEST_FILTER ?= *
CTEST_ARGS   ?= --output-on-failure -j
FUZZ_ARGS     ?= -runs=100000
VALGRIND_ARGS ?= --leak-check=full --error-exitcode=1
BENCH_ITERS   ?= 10000
PERF_REPEAT   ?= 5
PERF_EVENTS   ?= task-clock,context-switches,page-faults,cpu-migrations

# --------------------------------------------
# Help
# --------------------------------------------
.PHONY: help
help:
	@echo "Common targets:"
	@echo "  make configure            - CMake configure (debug preset)"
	@echo "  make build                - Build (debug)"
	@echo "  make test                 - Run ctest (debug)"
	@echo "  make valgrind             - Run tests under Valgrind (debug)"
	@echo "  make asan                 - Build (asan) and run ctest"
	@echo "  make ubsan                - Build (ubsan) and run ctest"
	@echo "  make tidy                 - clang-tidy all tracked sources"
	@echo "  make cppcheck             - cppcheck using compile_commands"
	@echo "  make format               - clang-format all tracked sources"
	@echo "  make coverage             - Build with LLVM coverage and show report"
	@echo "  make clean                - Remove build/"

# --------------------------------------------
# Configure & Build
# --------------------------------------------
.PHONY: configure build
configure:
	cmake --preset debug

build: configure
	cmake --build --preset debug -j

# --------------------------------------------
# Testing (Debug)
# --------------------------------------------
.PHONY: test
test: build
	ctest --preset debug $(CTEST_ARGS)

# Valgrind on the debug binary (direct test exe, not ctest)
.PHONY: valgrind
valgrind: build
	@if [[ ! -x "$(TEST_BIN_DEBUG)" ]]; then \
	  echo "Test binary not found at $(TEST_BIN_DEBUG). Adjust TEST_BIN_DEBUG if needed."; exit 1; \
	fi
	valgrind $(VALGRIND_ARGS) $(TEST_BIN_DEBUG) --gtest_filter=$(GTEST_FILTER)

# --------------------------------------------
# Sanitizers
# --------------------------------------------
.PHONY: asan ubsan
asan:
	cmake --preset asan
	cmake --build --preset asan -j
	ctest --preset asan $(CTEST_ARGS)

ubsan:
	cmake --preset ubsan
	cmake --build --preset ubsan -j
	ctest --preset ubsan $(CTEST_ARGS)

# --------------------------------------------
# Coverage (LLVM source-based)
# --------------------------------------------
PROFRAW  := $(COVERAGE_DIR)/tests.profraw
PROFDATA := $(COVERAGE_DIR)/tests.profdata
COV_BIN  := $(COVERAGE_DIR)/tests

.PHONY: coverage
coverage:
	cmake --preset coverage
	cmake --build --preset coverage -j
	LLVM_PROFILE_FILE="$(PROFRAW)" $(COV_BIN)
	llvm-profdata merge -sparse $(PROFRAW) -o $(PROFDATA)
	llvm-cov report $(COV_BIN) -instr-profile=$(PROFDATA) \
	  --ignore-filename-regex='build/.*|tests/.*|third_party/.*'
	@echo ""
	@echo "HTML report: $(COVERAGE_DIR)/html/index.html"
	llvm-cov show $(COV_BIN) -instr-profile=$(PROFDATA) \
	  --format=html --output-dir=$(COVERAGE_DIR)/html \
	  --ignore-filename-regex='build/.*|tests/.*|third_party/.*'

# --------------------------------------------
# Static Analysis
# --------------------------------------------
# clang-tidy uses the compile DB from the debug build dir.
.PHONY: tidy
tidy: build
	@FILES=$$(find src -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) ! -path './build/*'); \
	if [[ -z "$$FILES" ]]; then echo "No C++ files to analyze."; exit 0; fi; \
	clang-tidy -p $(DEBUG_DIR) -header-filter=.* --quiet $$FILES

# cppcheck prefers the compile DB too (faster, more accurate).
.PHONY: cppcheck
cppcheck: build
	cppcheck --project=$(DEBUG_DIR)/compile_commands.json \
	  --enable=warning,style,performance,portability \
	  --std=c++23 --inline-suppr \
	  --template=gcc --quiet \
	  --suppressions-list=cppcheck.suppress

# --------------------------------------------
# Formatting
# --------------------------------------------
.PHONY: format
format:
	@find . -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' -o -name '*.h' -o -name '*.hpp'\) ! -path './build/*' 2>/dev/null | xargs -r clang-format -i

# --------------------------------------------
# Clean
# --------------------------------------------
.PHONY: clean
clean:
	rm -rf build
