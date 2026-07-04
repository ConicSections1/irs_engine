# Compiler settings
CXX ?= g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Iinclude -O3

# Directories
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Find all source files and define object files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# The final executable name
TARGET = $(BIN_DIR)/pricer

# Default target
all: $(TARGET)

# Link the object files into the final binary
$(TARGET): $(OBJECTS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "Build successful! Run with ./$(TARGET)"

# Compile each source file into an object file
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean
