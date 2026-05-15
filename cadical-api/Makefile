all: prova 

prova: prova.o cadical-rel-3.0.0/build/libcadical.a
	g++ -Wall -O3 prova.o cadical-rel-3.0.0/build/libcadical.a -o prova

prova.o: prova.cc
	g++ -Wall -O3 -c prova.cc

