CXX ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -Iinclude
ASAN_FLAGS := -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -Iinclude

CORE_SRCS := \
	src/core/sha256.cpp \
	src/core/format.cpp \
	src/core/json.cpp \
	src/core/rules.cpp \
	src/core/ntfs_mft.cpp \
	src/core/shell_integration.cpp \
	src/core/scanner.cpp \
	src/core/duplicates.cpp \
	src/core/treemap.cpp \
	src/core/quarantine.cpp \
	src/core/config.cpp \
	src/core/reporter.cpp

BUILD_DIR := build

.PHONY: all cli test asan clean

all: cli test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

cli: $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CORE_SRCS) src/cli/main_cli.cpp -o $(BUILD_DIR)/reclaim-cli

test: $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CORE_SRCS) tests/test_suite.cpp -o $(BUILD_DIR)/reclaim_tests
	./$(BUILD_DIR)/reclaim_tests

asan: $(BUILD_DIR)
	$(CXX) $(ASAN_FLAGS) $(CORE_SRCS) tests/test_suite.cpp -o $(BUILD_DIR)/reclaim_tests_asan
	./$(BUILD_DIR)/reclaim_tests_asan

clean:
	rm -rf $(BUILD_DIR)
