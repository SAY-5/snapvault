# snapvault build orchestration.
#
# Targets:
#   make build   build both the C++ engine and the Go distribution layer
#   make test    run the C++ CTest suites and the Go race tests
#   make demo    end-to-end: mock dataset -> snapshot -> distribute -> fail a
#                node -> parallel restore -> byte-for-byte integrity check
#   make clean   remove build artifacts and demo scratch

SHELL := /bin/bash
BUILD_DIR := core/build
BIN_DIR := bin
DEMO_DIR := demo

.PHONY: all build build-cpp build-go test test-cpp test-go demo clean

all: build

build: build-cpp build-go

build-cpp:
	cmake -S core -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR)

build-go:
	mkdir -p $(BIN_DIR)
	go build -o $(BIN_DIR)/snapvault ./cmd/snapvault

test: test-cpp test-go

test-cpp: build-cpp
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-go:
	go vet ./...
	go test ./... -race

demo: build
	@bash scripts/demo.sh

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(DEMO_DIR)
