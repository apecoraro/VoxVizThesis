#ifndef GIGA_VOXELS_READER_H
#define GIGA_VOXELS_READER_H

#include "VoxVizCore/Referenced.h"
#include "VoxVizCore/SmartPtr.h"

#include "GigaVoxels/GigaVoxelsSceneGraph.h"

#include <string>

namespace gv
{
    class Group;

    class GigaVoxelsReader
    {
    public:
        static bool IsGigaVoxelsFile(const std::string& filename);
        static gv::Node* Load(const std::string& filename);
        static GigaVoxelsOctTree* LoadOctTreeFile(const std::string& filename);
        static void SetLoadNormals(bool loadNormals);
    };
}

#endif