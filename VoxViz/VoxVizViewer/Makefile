include ../make.include

INCLUDES+=-I../
# set up compiler and options
CXX = g++
CXXFLAGS = -g $(INCLUDES)

#-----Suffix Rules---------------------------
# set up C++ suffixes and relationship between .cc and .o files

.SUFFIXES: .cpp

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $<

.cpp :
	$(CXX) $(CXXFLAGS) $< -o $@ 

#-----File Dependencies----------------------

SRC = main.cpp

EXE = ../bin/VoxVizViewer

LIBS =  ../lib/GigaVoxels.a ../lib/RayCaster.a ../lib/VolumeSlicer3D.a ../lib/MarchingCubes.a ../lib/VoxVizOpenGL.a ../lib/VoxVizCore.a ../glew-1.7.0/lib/libGLEW.a
EXTERNAL_LIBS = -lQtCore -lQtGui -lQtOpenGL -lGLU -lGL 
      
OBJ = $(addsuffix .o, $(basename $(SRC)))

$(EXE): $(OBJ) $(LIBS)
	$(CXX) $(CXXFLAGS) $(LIBDIRS) $(OBJ) $(EXTERNAL_LIBS) $(LIBS) -o $@

clean:
	rm -f $(OBJ) $(EXE)

