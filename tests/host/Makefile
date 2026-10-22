# Host-side unit tests for the pure (Arduino-free) logic of the watch firmware.
# Not part of the sketch: Arduino IDE compiles only the sketch root and src/.
# Run: make -C tests/host

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -I../..

test_runner: test_main.cpp ../../GestureClassifier.h ../../BatteryLevel.h ../../NightPalette.h ../../DemoValues.h
	$(CXX) $(CXXFLAGS) -o $@ test_main.cpp

.PHONY: run clean
run: test_runner
	./test_runner

clean:
	rm -f test_runner

.DEFAULT_GOAL := run
