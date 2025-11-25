# --- 1. Project Configuration ---

# The name of the final executable
TARGET_EXEC := chess_engine

# The compiler to use
CC := gcc

# Directories
SRC_DIR := .
BUILD_DIR := build

# Source files (wildcard selects all .c files in current dir)
SRCS := $(wildcard $(SRC_DIR)/*.c)

# Object files (maps .c files to build/%.o)
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# Dependency files (generated automatically by GCC)
DEPS := $(OBJS:.o=.d)

# --- 2. Compiler Flags ---

# Standard warning flags (very high strictness to catch bugs)
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion

# Standard mode
STD_FLAG := -std=c17

# Dependency generation flags (-MMD generates dependencies, -MP fixes errors if headers are deleted)
DEP_FLAGS := -MMD -MP

# Final Compiler Flags
# (Includes directories, warnings, standard, and dependency logic)
CFLAGS := $(WARNINGS) $(STD_FLAG) $(DEP_FLAGS)

# Linker flags (if you need -lm for math, add it here)
LDFLAGS := 

# --- 3. Debug vs Release Build Settings ---

# Usage: "make DEBUG=1" for debug mode, "make" for release mode.
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O3 -DNDEBUG
endif

# =========================================================================
# --- 4. Targets ---
# =========================================================================

# Default target (what runs when you type just 'make')
all: $(BUILD_DIR)/$(TARGET_EXEC)
	@echo "Build complete: $(BUILD_DIR)/$(TARGET_EXEC)"

# Link the executable
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS)
	@echo "Linking $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Compile source files into object files
# This rule ensures the build directory exists before compiling
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# Include dependencies (if they exist) to trigger recompilation when headers change
-include $(DEPS)

# --- Helper Targets ---

# Run the game
run: all
	@echo "Running game..."
	@./$(BUILD_DIR)/$(TARGET_EXEC)

# Clean up build artifacts
clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR)

# Deep clean (removes build dir and any save files like board.txt if you want to reset)
distclean: clean
	@echo "Removing save files..."
	@rm -f board.txt

# Help message
help:
	@echo "Available targets:"
	@echo "  make          : Build the release version (optimized)"
	@echo "  make DEBUG=1  : Build the debug version (with symbols)"
	@echo "  make run      : Build and run the game"
	@echo "  make clean    : Remove compiled files"
	@echo "  make distclean: Remove compiled files along with saved board"

.PHONY: all clean distclean run help