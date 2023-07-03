CXX = g++
CXXFLAGS = -march=native -Wall -Wextra -Wpedantic -std=c++17
SRC_DIR = src
BUILD_DIR = build
TARGET = lishex

### Debugging (gdb)
debug ?= no
ifeq ($(debug),yes)
	CXXFLAGS += -ggdb -DDEBUG -w #-DNO_TT -DNO_NMH
endif

### Optimizations (on by default)
optimize ?= yes
ifeq ($(optimize),yes)
	ifneq ($(debug),yes)
		CXXFLAGS += -Ofast -fno-exceptions # -DNO_TT # -DNO_NMH
	endif
endif

# List of source files
SRCS = $(wildcard $(SRC_DIR)/*.cpp)

# List of object files
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

all: $(TARGET)

# Compile source files into object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

### $(CXX) $(CXXFLAGS) $(INC_DIRS) -c $< -o $@

# Link object files into executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

.PHONY: help run clean

# Clean the build directory
clean:
	rm -f $(BUILD_DIR)/*.o $(TARGET)

# Run the executable
run: $(TARGET)
	./$(TARGET)

help:
	@echo "To compile LiSHeX, type: "
	@echo "make"
	@echo "To compile with debug options, type: "
	@echo "make debug=yes"
	@echo "To compile without optimizations, type: "
	@echo "make optimize=no"
