CXX ?= g++
CXXFLAGS ?= -O3 -std=c++17 -march=native
CPPFLAGS ?= -I../inc -I$(STB_DIR)
LDFLAGS ?=
LDLIBS ?= -pthread

STB_DIR ?= ../build/_deps/stb-src

TARGET ?= blur_test
SRCS = main.cpp ../src/image_convolver.cpp ../src/thread_pool.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
