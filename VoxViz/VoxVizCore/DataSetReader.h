#ifndef VOX_DATASET_READER_H
#define VOX_DATASET_READER_H

#include "VoxVizCore/VolumeDataSet.h"

namespace vox
{
    class DataSetReader
    {
    public:
        VolumeDataSet* readVolumeDataFile(const std::string& inputFile);

        static std::string GetFileExtension(const std::string& fileName);
        static std::string GetFilePath(const std::string& fileName);

    };
}

#endif
