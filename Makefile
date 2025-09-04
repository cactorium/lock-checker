# Flags for the C++ compiler: C++11 and all the warnings
CXXFLAGS = -std=c++11 -Wall -fno-rtti
# Workaround for an issue of -std=c++11 and the current GCC headers
CXXFLAGS += -Wno-literal-suffix

# Determine the plugin-dir and add it to the flags
PLUGINDIR=$(shell $(CXX) -print-file-name=plugin)
CXXFLAGS += -I$(PLUGINDIR)/include

LDFLAGS = -std=c++20

# top level goal: build our plugin as a shared library
all: my_plugin.so

my_plugin.so: my_plugin.o
	$(CXX) $(LDFLAGS) -shared -o $@ $<

my_plugin.o : main.cc
	$(CXX) $(CXXFLAGS) -fPIC -c -o $@ $<

clean:
	rm -f warn_unused.o warn_unused.so

check: my_plugin.so main.cc
	$(CXX) -fplugin=./my_plugin.so -c test.cc -o /dev/null
 
.PHONY: all clean check
