#include "VoxVizCore/VoxSampler.h"

using namespace vox;

VoxSampler::VoxSampler(VolumeDataSet& dataSet,
                       size_t sampleStride,
                       size_t sampleCount) :
    m_pDataSet(&dataSet),
    m_sampleStride(sampleStride),
    m_curSampleX(0),
    m_curSampleY(0),
    m_curSampleZ(0)
{
}

bool VoxSampler::getNextSample(VoxSample& sample)
{
    VolumeDataSet& dataSet = *m_pDataSet;

    if(m_curSampleZ >= dataSet.dimZ() - 1)
    {
        return false;
    }

    //sw lowest grid value
    sample.values[0] = dataSet.valueAsFloat(m_curSampleX, 
                                            m_curSampleY, 
                                            m_curSampleZ);
    sample.positions[0] = dataSet.xyz(m_curSampleX, 
                                     m_curSampleY, 
                                     m_curSampleZ);

    //se lowest grid value
    sample.values[1] = dataSet.valueAsFloat(m_curSampleX + m_sampleStride, 
                                            m_curSampleY, 
                                            m_curSampleZ);
    sample.positions[1] = dataSet.xyz(m_curSampleX + m_sampleStride, 
                                     m_curSampleY, 
                                     m_curSampleZ);

    //ne lowest grid value
    sample.values[2] = dataSet.valueAsFloat(m_curSampleX + m_sampleStride, 
                                            m_curSampleY + m_sampleStride, 
                                            m_curSampleZ);
    sample.positions[2] = dataSet.xyz(m_curSampleX + m_sampleStride, 
                                     m_curSampleY + m_sampleStride, 
                                     m_curSampleZ);

    //nw lowest grid value
    sample.values[3] = dataSet.valueAsFloat(m_curSampleX, 
                                            m_curSampleY + m_sampleStride, 
                                            m_curSampleZ);
    sample.positions[3] = dataSet.xyz(m_curSampleX, 
                                     m_curSampleY + m_sampleStride, 
                                     m_curSampleZ);

    //sw highest grid value
    sample.values[4] = dataSet.valueAsFloat(m_curSampleX, 
                                            m_curSampleY, 
                                            m_curSampleZ + m_sampleStride);
    sample.positions[4] = dataSet.xyz(m_curSampleX, 
                                     m_curSampleY, 
                                     m_curSampleZ + m_sampleStride);

    //se highest grid value
    sample.values[5] = dataSet.valueAsFloat(m_curSampleX + m_sampleStride, 
                                            m_curSampleY, 
                                            m_curSampleZ + m_sampleStride);
    sample.positions[5] = dataSet.xyz(m_curSampleX + m_sampleStride, 
                                     m_curSampleY, 
                                     m_curSampleZ + m_sampleStride);

    //ne highest grid value
    sample.values[6] = dataSet.valueAsFloat(m_curSampleX + m_sampleStride, 
                                            m_curSampleY + m_sampleStride, 
                                            m_curSampleZ + m_sampleStride);
    sample.positions[6] = dataSet.xyz(m_curSampleX + m_sampleStride, 
                                     m_curSampleY + m_sampleStride, 
                                     m_curSampleZ + m_sampleStride);

    //nw highest grid value
    sample.values[7] = dataSet.valueAsFloat(m_curSampleX, 
                                            m_curSampleY + m_sampleStride, 
                                            m_curSampleZ + m_sampleStride);
    sample.positions[7] = dataSet.xyz(m_curSampleX, 
                                     m_curSampleY + m_sampleStride, 
                                     m_curSampleZ + m_sampleStride);

    m_curSampleX += m_sampleStride;
    if(m_curSampleX >= dataSet.dimX()-1)
    {
        m_curSampleX = 0;
        m_curSampleY += m_sampleStride;
        if(m_curSampleY >= dataSet.dimY()-1)
        {
            m_curSampleX = 0;
            m_curSampleY = 0;
            m_curSampleZ += m_sampleStride;
        }
    }

    return true;
}
