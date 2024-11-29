# Makefile for file_copier.cpp with OpenMP and OpenSSL

# Compiler
CXX := g++

# Compiler Flags
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -fopenmp

# Include Directories (if OpenSSL is in a non-standard location, add -I/path/to/openssl/include)
INCLUDES :=

# Library Flags
# -lssl and -lcrypto link against OpenSSL's SSL and Crypto libraries
LIBS := -lssl -lcrypto

# Source Files
SRC := SoloCopy.cpp

# Object Files
OBJ := $(SRC:.cpp=.o)

# Executable Name
EXEC := SoloCopy

# Default Target
all: $(EXEC)

# Link the executable
$(EXEC): $(OBJ)
	$(CXX) $(CXXFLAGS) $(OBJ) -o $(EXEC) $(LIBS)

# Compile source files to object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJ) $(EXEC)

# Phony Targets
.PHONY: all clean
