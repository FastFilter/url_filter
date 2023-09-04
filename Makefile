index: src/index.cpp
	$(CXX) $(CFLAGS) -o index src/index.cpp -O3 -I src -std=c++17 -Wall -Wextra

clean:
	rm -rf index