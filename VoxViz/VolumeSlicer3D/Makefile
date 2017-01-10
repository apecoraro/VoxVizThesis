include ../make.include

INCLUDES += -I../
# set up compiler and options
CXX = g++
CXXFLAGS = -g $(INCLUDES)
LIB = ../lib/VolumeSlicer3D.a

#-----Suffix Rules---------------------------
# set up C++ suffixes and relationship between .cc and .o files

.SUFFIXES: .cpp

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.cpp :
	$(CXX) $(CXXFLAGS) $< -o $@ 

#-----File Dependencies----------------------

SRC = VolumeSlicer3DRenderer.cpp
      
      
OBJ = $(addsuffix .o, $(basename $(SRC)))

$(LIB): $(OBJ)
	$(AR) rc $@ $(OBJ)

clean:
	rm -f $(OBJ) $(LIB)

