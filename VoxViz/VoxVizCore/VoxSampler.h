#ifndef VOX_VOX_SAMPLER_H
#define VOX_VOX_SAMPLER_H

#include "VoxVizCore/VolumeDataSet.h"

#include <QtGui/qvector3d.h>

#include <vector>

namespace vox
{
    typedef std::vector<float> Values;
    typedef std::vector<QVector3D> Positions;

    struct VoxSample
    {
        VoxSample(size_t sampleCount) : values(sampleCount, 0.0f), positions(sampleCount) {}
        Values values;
        Positions positions;
    };

    class VoxSampler
    {
    private:
        VolumeDataSet* m_pDataSet; 
        size_t m_sampleStride;

        size_t m_curSampleX;
        size_t m_curSampleY;
        size_t m_curSampleZ;

    public:
        VoxSampler(VolumeDataSet& dataSet,
                   size_t sampleStride,
                   size_t sampleCount);

        void setVolumeDataSet(VolumeDataSet& dataSet) 
        { 
            m_pDataSet = &dataSet; 
            m_curSampleX = m_curSampleY = m_curSampleZ = 0;
        }

        bool getNextSample(VoxSample& sample);
    };
};
#endif
