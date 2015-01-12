.PHONY : all
all : astaire

.PHONY : clean
clean :
	-rm astaire *.o

%.o : %.cpp astaire.hpp Makefile
	g++ -std=c++11 -g -gdwarf-2 -O0 -c -o $@ $(filter %.cpp, $^)

astaire : main.o astaire.o
	g++ -o astaire $^
