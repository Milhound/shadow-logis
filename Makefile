# SPDX-License-Identifier: AGPL-3.0-only
# GNU Make is a command front end; CMake/Ninja still build the application.
.DEFAULT_GOAL := help
# Packaging scripts share the source ZIP. Compile concurrency is controlled by JOBS.
.NOTPARALLEL:

CMAKE ?= cmake
CTEST ?= ctest
BASH ?= bash
POWERSHELL ?= powershell.exe
BUILD_DIR ?= build
BUILD_TYPE ?= Release
GENERATOR ?= Ninja
JOBS ?= 4
LINUX_FORMAT ?= TGZ
CMAKE_ARGS ?=
ARGS ?=
QT_PREFIX ?= $(if $(QT_ROOT_DIR),$(QT_ROOT_DIR),$(CMAKE_PREFIX_PATH))
export CMAKE_BUILD_PARALLEL_LEVEL := $(JOBS)

ifeq ($(OS),Windows_NT)
SHELL := cmd.exe
.SHELLFLAGS := /D /C
PYTHON ?= python
HOST_OS := Windows
APP := $(BUILD_DIR)/shadow-logis.exe
ifneq ($(strip $(QT_PREFIX)),)
RUN_ENV = $(CMAKE) -E env "PATH=$(QT_PREFIX)/bin;$(if $(PATH),$(PATH),$(Path))"
endif
else
SHELL := /bin/sh
PYTHON ?= python3
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
APP := $(BUILD_DIR)/shadow-logis.app/Contents/MacOS/shadow-logis
else
APP := $(BUILD_DIR)/shadow-logis
endif
endif

.PHONY: help all configure build debug release test run clean check-publication source
.PHONY: package linux deb rpm tarball arch macos windows
.PHONY: docker-deb docker-rpm docker-linux

help:
	@echo "Development: make build, test, run, debug, release, clean"
	@echo "Packages:    make deb, rpm, tarball, arch, macos, windows"
	@echo "Docker:      make docker-deb, docker-rpm, docker-linux - Linux x86-64 packages"
	@echo "Host package: make package; Linux defaults to TGZ, or LINUX_FORMAT=ARCH/DEB/RPM"
	@echo "Source:      make check-publication, source"
	@echo "Settings:    JOBS=4 BUILD_DIR=build BUILD_TYPE=Release QT_PREFIX=path"
	@echo "Extra CMake options: CMAKE_ARGS; application arguments: ARGS"
	@echo "Native targets require the matching OS; Docker targets provide Linux build environments."
	@echo "Package targets build, test and verify; they do not install."

all: build

configure:
	$(CMAKE) -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" "-DCMAKE_BUILD_TYPE=$(BUILD_TYPE)" -DBUILD_TESTING=ON $(if $(strip $(QT_PREFIX)),"-DCMAKE_PREFIX_PATH=$(QT_PREFIX)") $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel "$(JOBS)"

debug:
	$(MAKE) build BUILD_DIR=build-debug BUILD_TYPE=Debug

release:
	$(MAKE) build BUILD_DIR=build-release BUILD_TYPE=Release

test: build
	$(RUN_ENV) $(CTEST) --test-dir "$(BUILD_DIR)" --build-config "$(BUILD_TYPE)" --output-on-failure --parallel "$(JOBS)"

run: build
	$(RUN_ENV) "$(APP)" $(ARGS)

# CMake cleans generated outputs only; never recursively delete a supplied path.
clean:
	$(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --target clean

check-publication:
	$(PYTHON) tools/check-publication.py

source: check-publication
	$(PYTHON) tools/package-source.py

ifeq ($(HOST_OS),Windows)
package: windows
else ifeq ($(HOST_OS),Darwin)
package: macos
else
package: linux
endif

linux:
	$(BASH) tools/build-linux.sh "$(LINUX_FORMAT)"

deb:
	$(BASH) tools/build-linux.sh DEB

rpm:
	$(BASH) tools/build-linux.sh RPM

tarball:
	$(BASH) tools/build-linux.sh TGZ

arch:
	$(BASH) tools/build-linux.sh ARCH

macos:
	$(if $(strip $(QT_PREFIX)),$(CMAKE) -E env "CMAKE_PREFIX_PATH=$(QT_PREFIX)") $(BASH) tools/build-macos.sh

windows:
	$(POWERSHELL) -NoProfile -File tools/build-windows.ps1 $(if $(strip $(QT_PREFIX)),-QtPrefix "$(QT_PREFIX)") -Jobs "$(JOBS)"

docker-deb:
	$(BASH) tools/build-docker.sh DEB

docker-rpm:
	$(BASH) tools/build-docker.sh RPM

docker-linux: docker-deb docker-rpm
