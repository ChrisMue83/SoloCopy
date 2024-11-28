# Compiler
CXX = g++

# Compiler Flags
CXXFLAGS = -std=c++20 -pthread -O3 -march=native -flto -DNDEBUG -pipe -Wall -Wextra -Werror

# Linker Flags
LDFLAGS = -lssl -lcrypto -flto

# Target Executable
TARGET = SoloCopy

# Source Files
SOURCES = SoloCopy.cpp

# Object Files (Not strictly necessary for single-file projects, but good for scalability)
OBJECTS = $(SOURCES:.cpp=.o)

# Default Target
all: $(TARGET)

# Link the executable
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

# Compile source files to object files (optional for single-file projects)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(TARGET) $(OBJECTS)

# Phony Targets
.PHONY: all clean
