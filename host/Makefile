CC ?= cc
CXX ?= c++
INC = -I. -Ishim -I../components/fest/include -I../components/mqart/include -I../main
SRC_C = preview.c ../components/fest/src/fest.c ../components/fest/src/scene.c ../components/mqart/src/mqart.c
preview: $(SRC_C) ../main/menu.cpp stubs.h
	$(CXX) -O1 -g -Wall $(INC) -c ../main/menu.cpp -o menu.o
	$(CC) -O1 -g -Wall -fsanitize=address,undefined $(INC) -o $@ $(SRC_C) menu.o -lc++
	rm -f menu.o
clean:
	rm -f preview menu.o
