first = ${CXX}
second = clang++
third = 1
fourth = 1
a = 1
b = 2

ifeq ($(a), $(b))
a = 3
else
b = 4
endif

main: test.cpp
	@clang++ test.cpp -o main

ifeq ($(first), $(second))
	@echo "first == second"
else
	@echo "first != second"
endif

target: test.cpp
	@g++ test.cpp -o target

ifeq ($(third), $(fourth))
	@echo "third == fourth"
else
	@echo "third != fourth"
endif

	@echo "11111"

.PHONY: clean_main clean_target
clean_main:
	@rm main

clean_target:
	@rm target