# Makefile for Ferry Transport Simulation (Java)

JAVAC   = javac
JAVA    = java
SRC     = FerrySim.java
MAIN    = FerrySim

.PHONY: all clean run

all: compile

compile:
	$(JAVAC) $(SRC)

run: compile
	$(JAVA) $(MAIN)

clean:
	rm -f *.class
