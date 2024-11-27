CXX = g++
CXXFLAGS = -std=c++20 -pthread

TARGET = SoloCopy
SOURCES = SoloCopy.cpp

# Linker-Flags
LDFLAGS = -lssl -lcrypto

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)
