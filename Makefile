CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra

all: ipk-rdt

ipk-rdt: src/main.o src/args.o src/RDTClient.o src/RDTServer.o src/socket_utils.o
	$(CXX) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f ipk-rdt src/*.o

.PHONY: all clean