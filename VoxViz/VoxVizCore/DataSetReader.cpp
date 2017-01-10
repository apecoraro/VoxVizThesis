#include "VoxVizCore/DataSetReader.h"
#include "VoxVizCore/PVMReader.h"
#include "VoxVizCore/SmartPtr.h"

#include <QtCore/QFileInfo>
#include <QtCore/QDir>

#include <fstream>
#include <sstream>
#include <iostream>

using namespace vox;

static const char * const PATH_SEPARATORS = "/\\";
static unsigned int PATH_SEPARATORS_LEN = 2;

std::string DataSetReader::GetFilePath(const std::string& fileName)
{
    std::string::size_type slash = fileName.find_last_of(PATH_SEPARATORS);
    if (slash==std::string::npos) 
        return std::string();
    else 
        return std::string(fileName, 0, slash);
}

std::string DataSetReader::GetFileExtension(const std::string& fileName)
{
    static const char * const PATH_SEPARATORS = "/\\";

    std::string::size_type dot = fileName.find_last_of('.');
    std::string::size_type slash = fileName.find_last_of(PATH_SEPARATORS);
    if (dot==std::string::npos || (slash!=std::string::npos && dot<slash)) 
        return std::string("");
    return std::string(fileName.begin()+dot+1, fileName.end());
}

VolumeDataSet* DataSetReader::readVolumeDataFile(const std::string& inputFile)
{
    std::string ext = GetFileExtension(inputFile);
    if(ext == "pvm")
    {
        return PVMReader::instance().readVolumeData(inputFile);
    }
    else if(ext == "gvx" || ext == "gvp")
    {
        return new VolumeDataSet(inputFile);
    }
    else
    {
        std::ifstream inputStream;

        inputStream.open(inputFile.c_str());

        if(inputStream.is_open() != true)
            return NULL;

        std::string text;

        inputStream >> text;
    
        if(text != "VOXEL_HEADER")
            return NULL;

        QVector3D pos;
        QQuaternion orient;
        double scale = 1.0;
        size_t dimX=0, dimY=0, dimZ=0;
        bool typeIsScalars = false;
        bool formatIsUByte = false;
        while(inputStream.eof() != true)
        {
            inputStream >> text;
            if(text == "POSITION")
            {
                double x, y, z;
                inputStream >> x >> y >> z;
                if(inputStream.fail())
                    break;
            
                pos.setX(x);
                pos.setY(y);
                pos.setZ(z);
            }
            else if(text == "ORIENTATION")
            {
                double x, y, z, angle;
                inputStream >> x >> y >> z >> angle;
                if(inputStream.fail())
                    break;
            
                orient = QQuaternion::fromAxisAndAngle(x, y, z, angle);
            }
            else if(text == "SCALE")
            {
                inputStream >> scale;
                if(inputStream.fail())
                    break;
            }
            else if(text == "DIMENSIONS")
            {
                inputStream >> dimX >> dimY >> dimZ;
                if(inputStream.fail())
                    break;
            }
            else if(text == "TYPE")
            {
                inputStream >> text;
                if(inputStream.fail())
                    break;
                typeIsScalars = (text == "SCALARS");
            }
            else if(text == "FORMAT")
            {
                inputStream >> text;
                if(inputStream.fail())
                    break;
                formatIsUByte = (text == "GL_RGBA8" || text == "GL_R8");
            }
            else if(text == "VOXEL_HEADER_END")
            {
                break;
            }
        }

        if(dimX == 0 || dimY == 0 || dimZ == 0)
            return NULL;

        SmartPtr<VolumeDataSet> spData = new VolumeDataSet(inputFile,
                                                           pos, orient, 
                                                           scale, scale, scale,
                                                           dimX, dimY, dimZ);
        inputStream >> text;
        if(text == "VOXEL_SCALARS")
        {
            spData->initVoxels();
            for(size_t z = 0; z < dimZ; ++z)
            {
                std::string voxSlice;
                inputStream >> voxSlice;
                if(voxSlice != "VOXEL_SLICE")
                    return NULL;

                for(size_t y = 0; y < dimY; ++y)
                {
                    for(size_t x = 0; x < dimX; ++x)
                    {
                        float value;
                        inputStream >> value;
                        if(inputStream.fail())
                            return NULL;
                        spData->value(x, y, z) = static_cast<unsigned char>(value * 255.0f);
                    }
                }
            }
        }
        else
        {
            size_t voxelCount = dimX * dimY * dimZ;
            if(text == "VOXEL_IMAGE_FILE")
            {
                std::string extBinaryFile = inputFile + ".voxb";
                //inputStream >> extBinaryFile;
                std::ifstream binaryInputStream;
                binaryInputStream.open(extBinaryFile,
                                       std::ios_base::in |
                                       std::ios_base::binary);
                if(binaryInputStream.is_open() == false)
                    return NULL;
                Vec4f* pVoxelColors = new Vec4f[voxelCount];
                binaryInputStream.read((char*)pVoxelColors,
                                       sizeof(Vec4f) * voxelCount);
                spData->setColors(pVoxelColors);
            }
            else if(text == "VOXEL_SUB_IMAGE_FILES")
            {
				QFileInfo fileInfo(QString(inputFile.c_str()));

				QDir baseDir = fileInfo.dir();

                while(inputStream.eof() != true)
                {
                    std::string subImageText;
                    inputStream >> subImageText;
                    if(subImageText != "VOXEL_SUB_IMAGE_FILE")
                        break;
                    unsigned int rangeStartX, rangeStartY, rangeStartZ;
                    inputStream >> rangeStartX;
                    inputStream >> rangeStartY;
                    inputStream >> rangeStartZ;
                    if(inputStream.fail())
                        return NULL;
                    unsigned int rangeEndX, rangeEndY, rangeEndZ;
                    inputStream >> rangeEndX;
                    inputStream >> rangeEndY;
                    inputStream >> rangeEndZ;
                    if(inputStream.fail())
                        return NULL;
                    //skip the space character
                    inputStream.seekg(1, std::ios_base::cur);
                    std::stringbuf extBinaryFile;
                    inputStream.get(extBinaryFile);
                    if(inputStream.fail())
                        return NULL;

					std::stringstream extBinaryFilePath;
					extBinaryFilePath << baseDir.absolutePath().toAscii().data()
									  << "/"
									  << extBinaryFile.str();

                    std::cout << "Loading sub-image: " 
							  << extBinaryFilePath.str() << std::endl;
					
                    std::ifstream binaryInputStream;
                    binaryInputStream.open(extBinaryFilePath.str(),
                                       std::ios_base::in |
                                       std::ios_base::binary);
                    if(binaryInputStream.is_open() == false)
                        return NULL;

                    VolumeDataSet::SubVolume::Format format = VolumeDataSet::SubVolume::FORMAT_UBYTE_RGBA;
                    if(formatIsUByte == true)
                    {
                        if(typeIsScalars == true)
                            format = VolumeDataSet::SubVolume::FORMAT_UBYTE_SCALARS;
                    }
                    else
                        format = VolumeDataSet::SubVolume::FORMAT_FLOAT_RGBA;

                    VolumeDataSet::SubVolume subVolume(rangeStartX, rangeStartY, rangeStartZ,
                                                       rangeEndX, rangeEndY, rangeEndZ,
                                                       format);
                    int ubyteFormat;
                    binaryInputStream.read((char*)&ubyteFormat, sizeof(int));
                    if((ubyteFormat == 0 && formatIsUByte) || (ubyteFormat != 0 && formatIsUByte == false))
                        return NULL;
                    
                    unsigned int dataSize;
                    binaryInputStream.read((char*)&dataSize, sizeof(unsigned int));
                    if(dataSize != subVolume.dataSize())
                        return NULL;
                    binaryInputStream.read((char*)subVolume.data(),
                                           subVolume.dataSize());
                    spData->addSubVolume(subVolume);
                }
            }
            else
            {
                Vec4ub* pVoxelColors = new Vec4ub[voxelCount];
                spData->setColors(pVoxelColors);
                for(size_t z = 0; z < dimZ; ++z)
                {
                    std::string voxSlice;
                    inputStream >> voxSlice;
                    if(voxSlice != "VOXEL_SLICE")
                        return NULL;

                    for(size_t y = 0; y < dimY; ++y)
                    {
                        for(size_t x = 0; x < dimX; ++x)
                        {
                            float r, g, b, a;
                            inputStream >> r >> g >> b >> a;
                            if(inputStream.fail())
                                return NULL;
                            spData->setColor(x, y, z,
                                             r, g, b, a);
                        }
                    }
                }
            }
        }

        return spData.release();
    }
}
