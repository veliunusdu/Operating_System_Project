# Makefile for Java Ferry Transport Simulation

JAVAC   = javac
JAVA    = java
SRC     = FerrySim.java
CLASS   = FerrySim.class

.PHONY: all clean run

all: $(CLASS)

$(CLASS): $(SRC)
	$(JAVAC) $(SRC)

run: $(CLASS)
	$(JAVA) FerrySim

clean:
	rm -f *.class
