include ../make.include

INCLUDES += -I../
# set up compiler and options
CXX = g++
CXXFLAGS = -g $(INCLUDES)
LIB = ../lib/RayCaster.a

#-----Suffix Rules---------------------------
# set up C++ suffixes and relationship between .cc and .o files

.SUFFIXES: .cpp

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.cpp :
	$(CXX) $(CXXFLAGS) $< -o $@ 

#-----File Dependencies----------------------

SRC = RayCastRenderer.cpp
      
      
OBJ = $(addsuffix .o, $(basename $(SRC)))

$(LIB): $(OBJ)
	$(AR) rc $@ $(OBJ)

clean:
	rm -f $(OBJ) $(LIB)

