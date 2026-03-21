# Makefile for cvimg - RTL firmware image tool
# Copyright (C) 2024

# Default target
.DEFAULT_GOAL := all

# Program name and version
PROGRAM = cvimg
VERSION = 2.1.0

# Build configuration
CC ?= gcc
CFLAGS = -std=c99 -Wall -Wextra -Werror -O2 -D_GNU_SOURCE
LDFLAGS = 
LIBS = 

# Debug build configuration
DEBUG_CFLAGS = -std=c99 -Wall -Wextra -Werror -O0 -g -DDEBUG -D_GNU_SOURCE
SANITIZE_CFLAGS = $(DEBUG_CFLAGS) -fsanitize=address -fsanitize=undefined

# Installation paths
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man/man1

# Cross-compilation support
CROSS_COMPILE ?=
ifneq ($(CROSS_COMPILE),)
    CC = $(CROSS_COMPILE)gcc
    STRIP = $(CROSS_COMPILE)strip
else
    STRIP = strip
endif

# Source files
SOURCES = cvimg.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = 

# Build targets
TARGETS = $(PROGRAM)
DEBUG_TARGET = $(PROGRAM)-debug
STATIC_TARGET = $(PROGRAM)-static

# Test files
TEST_DIR = tests
TEST_SOURCES = $(wildcard $(TEST_DIR)/*.c)
TEST_TARGETS = $(TEST_SOURCES:.c=)

# Documentation
MAN_PAGE = $(PROGRAM).1
README = README.md

# Version information
BUILD_DATE = $(shell date '+%Y-%m-%d %H:%M:%S')
GIT_COMMIT = $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
VERSION_CFLAGS =

#
# Build rules
#

all: $(TARGETS)

$(PROGRAM): $(OBJECTS)
	@echo "  LINK    $@"
	@$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(HEADERS)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $(VERSION_CFLAGS) -c -o $@ $<

# Debug build
debug: CFLAGS = $(DEBUG_CFLAGS)
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): $(SOURCES) $(HEADERS)
	@echo "  CC-DBG  $@"
	@$(CC) $(DEBUG_CFLAGS) $(VERSION_CFLAGS) -o $@ $(SOURCES) $(LIBS)

# Static build
static: LDFLAGS += -static
static: $(STATIC_TARGET)

$(STATIC_TARGET): $(OBJECTS)
	@echo "  LINK-ST $@"
	@$(CC) $(LDFLAGS) -static -o $@ $^ $(LIBS)

# Sanitized build for testing
sanitize: CFLAGS = $(SANITIZE_CFLAGS)
sanitize: clean $(PROGRAM)

# Strip symbols for release
strip: $(TARGETS)
	@echo "  STRIP   $(TARGETS)"
	@$(STRIP) $(TARGETS)

#
# Testing
#

test: $(PROGRAM) test-basic test-signatures test-formats

test-basic: $(PROGRAM)
	@echo "Running basic functionality tests..."
	@./tests/test-basic.sh ./$(PROGRAM)

test-signatures: $(PROGRAM)
	@echo "Running signature tests..."
	@./tests/test-signatures.sh ./$(PROGRAM)

test-formats: $(PROGRAM)
	@echo "Running format tests..."
	@./tests/test-formats.sh ./$(PROGRAM)

# Memory leak testing
test-valgrind: debug
	@echo "Running valgrind tests..."
	@valgrind --leak-check=full --error-exitcode=1 ./$(DEBUG_TARGET) --help > /dev/null

# Performance testing
test-perf: $(PROGRAM)
	@echo "Running performance tests..."
	@time ./$(PROGRAM) -i tests/large-file.bin -o /tmp/test.img -e 0x80000000 -b 0x20000 -t kernel

#
# Code quality
#

# Static analysis with cppcheck
check: $(SOURCES)
	@echo "Running static analysis..."
	@cppcheck --enable=all --error-exitcode=1 --suppress=missingIncludeSystem $(SOURCES)

# Code formatting
format: $(SOURCES) $(HEADERS)
	@echo "Formatting code..."
	@clang-format -i $(SOURCES) $(HEADERS)

# Check code formatting
check-format: $(SOURCES) $(HEADERS)
	@echo "Checking code format..."
	@clang-format -Werror --dry-run $(SOURCES) $(HEADERS)

#
# Documentation
#

# Generate man page
man: $(MAN_PAGE)

$(MAN_PAGE): $(PROGRAM)
	@echo "Generating man page..."
	@help2man --no-info --name="RTL firmware image tool" ./$(PROGRAM) > $(MAN_PAGE)

# Generate README
readme: $(README)

$(README): $(PROGRAM)
	@echo "Generating README..."
	@echo "# $(PROGRAM) - RTL Firmware Image Tool" > $(README)
	@echo "" >> $(README)
	@echo "Version: $(VERSION)" >> $(README)
	@echo "Build: $(BUILD_DATE)" >> $(README)
	@echo "" >> $(README)
	@echo "## Usage" >> $(README)
	@echo "" >> $(README)
	@echo '```' >> $(README)
	@./$(PROGRAM) -h >> $(README)
	@echo '```' >> $(README)

#
# Installation
#

install: $(PROGRAM) $(MAN_PAGE)
	@echo "Installing $(PROGRAM)..."
	@install -d $(DESTDIR)$(BINDIR)
	@install -m 755 $(PROGRAM) $(DESTDIR)$(BINDIR)/
	@install -d $(DESTDIR)$(MANDIR)
	@install -m 644 $(MAN_PAGE) $(DESTDIR)$(MANDIR)/

uninstall:
	@echo "Uninstalling $(PROGRAM)..."
	@rm -f $(DESTDIR)$(BINDIR)/$(PROGRAM)
	@rm -f $(DESTDIR)$(MANDIR)/$(MAN_PAGE)

#
# Packaging
#

# Create source tarball
dist: clean
	@echo "Creating source distribution..."
	@mkdir -p dist
	@tar czf dist/$(PROGRAM)-$(VERSION).tar.gz \
		--exclude=dist \
		--exclude=.git \
		--exclude='*.o' \
		--exclude='$(PROGRAM)' \
		--exclude='$(DEBUG_TARGET)' \
		--exclude='$(STATIC_TARGET)' \
		--transform 's,^,$(PROGRAM)-$(VERSION)/,' \
		*

# Build Debian package
deb: $(PROGRAM)
	@echo "Building Debian package..."
	@mkdir -p dist/debian/DEBIAN
	@mkdir -p dist/debian$(BINDIR)
	@mkdir -p dist/debian$(MANDIR)
	@install -m 755 $(PROGRAM) dist/debian$(BINDIR)/
	@install -m 644 $(MAN_PAGE) dist/debian$(MANDIR)/
	@echo "Package: $(PROGRAM)" > dist/debian/DEBIAN/control
	@echo "Version: $(VERSION)" >> dist/debian/DEBIAN/control
	@echo "Architecture: $(shell dpkg --print-architecture 2>/dev/null || echo amd64)" >> dist/debian/DEBIAN/control
	@echo "Maintainer: Developer <dev@example.com>" >> dist/debian/DEBIAN/control
	@echo "Description: RTL firmware image creation tool" >> dist/debian/DEBIAN/control
	@dpkg-deb --build dist/debian dist/$(PROGRAM)_$(VERSION)_$(shell dpkg --print-architecture 2>/dev/null || echo amd64).deb

# Build RPM package
rpm: dist
	@echo "Building RPM package..."
	@rpmbuild -ta dist/$(PROGRAM)-$(VERSION).tar.gz

#
# Development helpers
#

# Setup development environment
dev-setup:
	@echo "Setting up development environment..."
	@mkdir -p tests
	@echo '#!/bin/bash' > tests/test-basic.sh
	@echo 'echo "Basic test: $$1 --help"' >> tests/test-basic.sh
	@echo '$$1 --help > /dev/null' >> tests/test-basic.sh
	@echo 'echo "Basic test passed"' >> tests/test-basic.sh
	@chmod +x tests/test-basic.sh
	@cp tests/test-basic.sh tests/test-signatures.sh
	@cp tests/test-basic.sh tests/test-formats.sh
	@echo "Development environment ready"

# Create test files
test-files:
	@echo "Creating test files..."
	@mkdir -p tests
	@dd if=/dev/zero of=tests/small-file.bin bs=1K count=1 2>/dev/null
	@dd if=/dev/zero of=tests/medium-file.bin bs=1K count=100 2>/dev/null
	@dd if=/dev/zero of=tests/large-file.bin bs=1M count=10 2>/dev/null
	@echo "Test files created"

# Git hooks
hooks:
	@echo "Installing git hooks..."
	@echo '#!/bin/sh' > .git/hooks/pre-commit
	@echo 'make check-format check' >> .git/hooks/pre-commit
	@chmod +x .git/hooks/pre-commit
	@echo "Git hooks installed"

#
# Cleanup
#

clean:
	@echo "Cleaning build files..."
	@rm -f $(OBJECTS) $(TARGETS) $(DEBUG_TARGET) $(STATIC_TARGET)
	@rm -f $(MAN_PAGE) core *.core
	@rm -rf *.dSYM

distclean: clean
	@echo "Cleaning distribution files..."
	@rm -rf dist/
	@rm -f $(README)

mrproper: distclean
	@echo "Cleaning everything..."
	@rm -rf tests/

#
# Help
#

help:
	@echo "Available targets:"
	@echo "  all          - Build the program (default)"
	@echo "  debug        - Build debug version"
	@echo "  static       - Build static version"
	@echo "  sanitize     - Build with sanitizers"
	@echo "  strip        - Strip symbols from binary"
	@echo ""
	@echo "Testing:"
	@echo "  test         - Run all tests"
	@echo "  test-valgrind- Run valgrind tests"
	@echo "  test-perf    - Run performance tests"
	@echo ""
	@echo "Code quality:"
	@echo "  check        - Run static analysis"
	@echo "  format       - Format source code"
	@echo "  check-format - Check code formatting"
	@echo ""
	@echo "Documentation:"
	@echo "  man          - Generate man page"
	@echo "  readme       - Generate README"
	@echo ""
	@echo "Installation:"
	@echo "  install      - Install program and man page"
	@echo "  uninstall    - Remove installed files"
	@echo ""
	@echo "Packaging:"
	@echo "  dist         - Create source tarball"
	@echo "  deb          - Build Debian package"
	@echo "  rpm          - Build RPM package"
	@echo ""
	@echo "Development:"
	@echo "  dev-setup    - Setup development environment"
	@echo "  test-files   - Create test files"
	@echo "  hooks        - Install git hooks"
	@echo ""
	@echo "Cleanup:"
	@echo "  clean        - Clean build files"
	@echo "  distclean    - Clean distribution files"
	@echo "  mrproper     - Clean everything"
	@echo ""
	@echo "Variables:"
	@echo "  CC=$(CC)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  PREFIX=$(PREFIX)"
	@echo "  CROSS_COMPILE=$(CROSS_COMPILE)"

#
# Phony targets
#

.PHONY: all debug static sanitize strip test test-basic test-signatures test-formats test-valgrind test-perf
.PHONY: check format check-format man readme install uninstall dist deb rpm
.PHONY: dev-setup test-files hooks clean distclean mrproper help

# Keep intermediate files
.PRECIOUS: %.o

# Disable built-in rules
.SUFFIXES:
