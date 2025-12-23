CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -pthread -Iinc
LDFLAGS ?= -pthread
BUILD_DIR ?= build

SOURCES = src/thread_pool.cpp
SIMPLE_TEST_SRC = simple_test.cpp
FULL_TEST_SRC = thread_pool_test.cpp

SIMPLE_TEST_BIN = $(BUILD_DIR)/simple_test
FULL_TEST_BIN = $(BUILD_DIR)/thread_pool_test

.PHONY: all simple_test full_test clean

all: simple_test full_test

simple_test: $(SIMPLE_TEST_BIN)

full_test: $(FULL_TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SIMPLE_TEST_BIN): $(SIMPLE_TEST_SRC) $(SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(FULL_TEST_BIN): $(FULL_TEST_SRC) $(SOURCES) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
