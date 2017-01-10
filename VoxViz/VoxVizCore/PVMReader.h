#ifndef VOX_PVM_READER_H
#define VOX_PVM_READER_H

#include <VoxVizCore/VolumeDataSet.h>

namespace vox
{
    class PVMReader
    {
    public:
        static PVMReader& instance();
        vox::VolumeDataSet* readVolumeData(const std::string& filename);
    private:
        PVMReader() {}
        PVMReader(const PVMReader&) {}
    };
};

#endif
