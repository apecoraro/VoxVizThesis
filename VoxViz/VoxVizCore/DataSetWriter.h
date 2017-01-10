#ifndef VOX_DATASET_WRITER_H
#define VOX_DATASET_WRITER_H

#include "VoxVizCore/Image.h"

namespace vox
{
    class DataSetWriter
    {
    public:
        bool writeImage(const Image& image, 
                        const std::string& outputName);
    };
}

#endif
