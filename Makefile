CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra

all: ipk-rdt

ipk-rdt: src/main.o src/args.o
	$(CXX) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f ipk-rdt src/*.o

.PHONY: all clean