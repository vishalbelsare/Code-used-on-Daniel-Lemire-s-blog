uname_p := $(shell uname -m)
$(info $(uname_p))

$(info $$uname_p is [${uname_p}])
ifeq ($(uname_p),aarch64)
arch = 
else
ifeq ($(uname_p),arm64)
else
arch = -march=native
endif
endif
all: bitmapdecoding 

bitmapdecoding.s: bitmapdecoding.cpp
	$(CXX) -O2 $(arch) -S -fverbose-asm -std=c++14 -o bitmapdecoding.s bitmapdecoding.cpp 
	

bitmapdecoding: bitmapdecoding.cpp
	$(CXX) --version
	$(CXX) -O3 $(arch) -std=c++14 -o bitmapdecoding bitmapdecoding.cpp -Wall -I.

sanibitmapdecoding : bitmapdecoding.cpp
	$(CXX) -O0 -g3 $(arch) -fsanitize=address -fno-omit-frame-pointer   -std=c++14 -o sanibitmapdecoding bitmapdecoding.cpp -Wall -I.
clean:
	rm -r -f bitmapdecoding bitmapdecoding.s bitmapdecoding_countbranch sanibitmapdecoding
