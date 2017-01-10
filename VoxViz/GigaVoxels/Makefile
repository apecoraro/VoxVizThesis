include ../make.include

INCLUDES += -I../ -I../glm-0.9.3.1
# set up compiler and options
CXX = g++
CXXFLAGS = -g $(INCLUDES)
LIB = ../lib/GigaVoxels.a

#-----Suffix Rules---------------------------
# set up C++ suffixes and relationship between .cc and .o files

.SUFFIXES: .cpp

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.cpp :
	$(CXX) $(CXXFLAGS) $< -o $@ 

#-----File Dependencies----------------------

SRC = GigaVoxelsOctTreeNodePool.cpp GigaVoxelsBrickPool.cpp GigaVoxelsOctTree.cpp GigaVoxelsRenderer.cpp
      
      
OBJ = $(addsuffix .o, $(basename $(SRC)))

$(LIB): $(OBJ)
	$(AR) rc $@ $(OBJ)

clean:
	rm -f $(OBJ) $(LIB)

