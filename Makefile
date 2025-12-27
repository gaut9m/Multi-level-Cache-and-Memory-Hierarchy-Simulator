CC = g++
OPT = -O3
WARN = -Wall
STD = -std=c++11
CFLAGS = $(OPT) $(WARN) $(STD)

SIM_SRC = sim.cc
SIM_OBJ = sim.o

all: sim

sim: $(SIM_OBJ)
	$(CC) -o sim $(CFLAGS) $(SIM_OBJ)

%.o: %.cc
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f *.o sim

clobber:
	rm -f *.o
