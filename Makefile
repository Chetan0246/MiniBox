CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -pedantic -std=c11 -D_GNU_SOURCE -Iruntime/include
BUILD_DIR ?= build
BIN_DIR ?= bin

TARGET = $(BIN_DIR)/minibox
RUNTIME_OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/process.o $(BUILD_DIR)/workload.o $(BUILD_DIR)/lifecycle.o $(BUILD_DIR)/isolation.o $(BUILD_DIR)/resources.o $(BUILD_DIR)/monitor.o
EXAMPLES = $(BIN_DIR)/examples/hello $(BIN_DIR)/examples/exit_code $(BIN_DIR)/examples/crash_test $(BIN_DIR)/examples/sleeper $(BIN_DIR)/examples/check_isolation $(BIN_DIR)/examples/fork_test $(BIN_DIR)/examples/memory_hog $(BIN_DIR)/examples/cpu_hog

.PHONY: all clean test test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7 test-phase8 test-behavioral benchmark demo examples

all: $(TARGET) examples

$(TARGET): $(RUNTIME_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: runtime/src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

examples: $(EXAMPLES)

$(BIN_DIR)/examples/%: examples/%.c | $(BIN_DIR)/examples
	$(CC) $(CFLAGS) $< -o $@

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR)/examples:
	mkdir -p $(BIN_DIR)/examples

test: all test-phase1 test-phase2 test-phase3 test-phase4 test-phase5 test-phase6 test-phase7 test-phase8 test-behavioral

test-phase1: all
	@echo "Running Phase 1 Test Suite..."
	@bash tests/test_phase1.sh

test-phase2: all
	@echo "Running Phase 2 Test Suite..."
	@bash tests/test_phase2.sh

test-phase3: all
	@echo "Running Phase 3 Test Suite..."
	@bash tests/test_phase3.sh

test-phase4: all
	@echo "Running Phase 4 Test Suite..."
	@bash tests/test_phase4.sh

test-phase5: all
	@echo "Running Phase 5 Test Suite..."
	@bash tests/test_phase5.sh

test-phase6: all
	@echo "Running Phase 6 Test Suite..."
	@bash tests/test_phase6.sh

test-phase7: all
	@echo "Running Phase 7 Test Suite..."
	@bash tests/test_phase7.sh

test-phase8: all
	@echo "Running Phase 8 Test Suite..."
	@bash tests/test_phase8.sh

test-behavioral: all
	@echo "Running Behavioral Verification Test Suite..."
	@bash tests/test_behavioral.sh

benchmark: all
	@.venv/bin/python3 benchmarks/benchmark.py

demo: all
	@bash demo/demo_script.sh

clean:
	rm -rf $(BIN_DIR) $(BUILD_DIR)

