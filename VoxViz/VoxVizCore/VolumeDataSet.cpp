#include "VoxVizCore/VolumeDataSet.h"

#include "VoxVizCore/DataSetReader.h"

#include <cmath>
#include <iostream>

using namespace vox;

std::string VolumeDataSet::getInputFileExtension() const
{
    return DataSetReader::GetFileExtension(m_inputFile);
}

BoundingSphere VolumeDataSet::computeBoundingSphere() const
{
    double maxScale = std::max(m_scaleX, std::max(m_scaleY, m_scaleZ));
    double maxDim = std::max(std::max(dimX(), dimY()), dimZ());
    BoundingSphere bounds(QVector3D(m_scaleX * (dimX()-1) * 0.5, 
                                    m_scaleY * (dimY()-1) * 0.5, 
                                    m_scaleZ * (dimZ()-1) * 0.5), 
                                    maxScale * maxDim * 0.5f);

    return bounds;
}

BoundingBox VolumeDataSet::computeBoundingBox() const
{
    BoundingBox bounds(0, 0, 0, 
                        m_scaleX * (dimX()-1), 
                        m_scaleY * (dimY()-1), 
                        m_scaleZ * (dimZ()-1));

    return bounds;
}

QVector3D VolumeDataSet::xyz(size_t x, size_t y, size_t z) const
{
    QVector3D xyz(x * m_scaleX, y * m_scaleY, z * m_scaleZ);

    const QMatrix4x4& transform = getTransform();

    return transform * xyz;
}

VolumeDataSet::~VolumeDataSet()
{
    freeVoxels();
}

void VolumeDataSet::freeVoxels()
{
    if(m_pVoxels)
    {
        delete [] m_pVoxels; 
        m_pVoxels = NULL;
    }
    if(m_pVoxelColorsUB)
    {
        delete [] m_pVoxelColorsUB;
        m_pVoxelColorsUB = NULL;
    }
    if(m_pVoxelColorsF)
    {
        delete [] m_pVoxelColorsF;
        m_pVoxelColorsF = NULL;
    }
}

static float GetAlpha(Vec4ub* pVoxelColors, 
                size_t dimX, size_t dimY, 
                size_t x, size_t y, size_t z)
{
    size_t index = (z * dimY * dimX) + (y * dimX) + x;
    unsigned char alpha = pVoxelColors[index].a;

    return static_cast<float>(alpha) / 255.0f;
}

void VolumeDataSet::generateFromSubVolumes()
{
    initVoxels();
    while(subVolumeCount() > 0)
    {
        SubVolume subVol;
        popSubVolume(subVol);

        if(subVol.getFormat() != SubVolume::FORMAT_UBYTE_SCALARS)
        {
            std::cerr << "VolumeDataSet::generateFromSubVolumes: Invalid SubVolume format "
                      << subVol.getFormat() 
                      << std::endl;
            continue;
        }

        unsigned int xSize = subVol.rangeEndX() - subVol.rangeStartX();
        unsigned int ySize = subVol.rangeEndY() - subVol.rangeStartY();
        unsigned int zSize = subVol.rangeEndZ() - subVol.rangeStartZ();
        unsigned int rangeZ = subVol.rangeStartZ();
        for(unsigned int z = 0; z < zSize; ++z, ++rangeZ)
        {
            unsigned int rangeY = subVol.rangeStartY();
            for(unsigned int y = 0; y < ySize; ++y, ++rangeY)
            {
                unsigned int readIndex = (z * ySize * xSize) + (y * xSize);
                unsigned int readRowSize = sizeof(Voxels) * xSize;
                Voxels* pReadPtr = &static_cast<Voxels*>(subVol.data())[readIndex];

                unsigned int rangeX = subVol.rangeStartX();
                unsigned int writeIndex = (rangeZ * m_dimY * m_dimX) + (rangeY * m_dimX) + rangeX;
                
                Voxels* pWritePtr = &m_pVoxels[writeIndex];
                memcpy(pWritePtr, pReadPtr, readRowSize);
            }
        }
    }
}

void VolumeDataSet::convert(const VolumeDataSet::ColorLUT& colorLUT,
                            Vec4ub* pVoxelColors,
                            Vec3f* pVoxelGrads/*=NULL*/) const
{
    size_t voxelCount = m_dimX * m_dimY * m_dimZ;
    
    for(size_t index = 0; 
        index < voxelCount; 
        ++index)
    {
         const vox::VolumeDataSet::Voxels& voxel = m_pVoxels[index];
         float voxelFloat = (static_cast<float>(voxel) / 255.0f);
         size_t voxelBase = static_cast<size_t>(voxelFloat * (colorLUT.size()-1));
         size_t voxelNext = voxelBase < colorLUT.size()-1 ? voxelBase+1 : voxelBase;

         float voxelInterp = 1.0f - (voxelFloat - std::floor(voxelFloat));
         QVector4D color = (colorLUT.at(voxelBase)*voxelInterp) 
                            + (colorLUT.at(voxelNext)*(1.0f - voxelInterp));
         
         pVoxelColors[index].r = static_cast<unsigned char>((color.x() * 255.0));
         pVoxelColors[index].g = static_cast<unsigned char>((color.y() * 255.0));
         pVoxelColors[index].b = static_cast<unsigned char>((color.z() * 255.0));
         pVoxelColors[index].a = static_cast<unsigned char>((color.w() * 255.0));
    }

    if(pVoxelGrads == NULL)
        return;

    for(size_t z = 0; z < m_dimZ; ++z)
    {
        for(size_t y = 0; y < m_dimY; ++y)
        {
            for(size_t x = 0; x < m_dimX; ++x)
            {
                vox::Vec3f sample1;
                vox::Vec3f sample2;
                if(x == 0)//clamp to border
                    sample1.x = 0.0f;
                else
                    sample1.x = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x-1, y, z);

                if(x == m_dimX-1)
                    sample2.x = 0.0f;
                else
                    sample2.x = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x+1, y, z);
                if(y == 0)
                    sample1.y = 0.0f;
                else
                    sample1.y = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x, y-1, z);

                if(y == m_dimY-1)
                    sample2.y = 0.0f;
                else
                    sample2.y = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x, y+1, z);

                if(z == 0)
                    sample1.z = 0.0f;
                else
                    sample1.z = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x, y, z-1);

                if(z == m_dimZ-1)
                    sample2.z = 0.0f;
                else
                    sample2.z = GetAlpha(pVoxelColors, 
                                         m_dimX, m_dimY,
                                         x, y, z+1);

                QVector3D normal;
                normal.setX(sample1.x - sample2.x);
                normal.setY(sample1.y - sample2.y);
                normal.setZ(sample1.z - sample2.z);
                qreal len = normal.length();
                if(len > 0)
                    normal /= len;
                else
                {
                    normal.setX(0);
                    normal.setY(0);
                    normal.setZ(0);
                }

                pVoxelGrads[(z * m_dimY * m_dimX) + (y * m_dimX) + x] = Vec3f(normal);
            }
        }
    }
}
