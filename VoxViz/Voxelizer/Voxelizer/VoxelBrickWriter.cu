#include "VoxelBrickWriter.h"

#include <iostream>
#include <sstream>
#include <direct.h>

#include <osg/Image>
#include <osg/Texture2D>
#include <osg/Texture3D>
#include <osg/GraphicsContext>
#include <osgViewer/GraphicsWindow>

using namespace cuda;

VoxelBrickWriter::VoxelBrickWriter() :
    _binary(false),
    _compressed(false)
{
}

VoxelBrickWriter::VoxelBrickWriter(const VoxelBrickWriter& copy) :
    _binary(copy._binary),
    _compressed(copy._compressed),
    _brickMap(copy._brickMap),
    _storedBrickMap(copy._storedBrickMap),
    _outputFileName(copy._outputFileName),
    _outputPartialBricksDir(copy._outputPartialBricksDir)
{
    _voxFile.swap(const_cast<VoxelBrickWriter&>(copy)._voxFile);
}

VoxelBrickWriter::~VoxelBrickWriter()
{
    //deleteOpenGLContext();
    if(_voxFile.is_open())
    {
        endBricksFile();
    }
}

VoxelBrickWriter& VoxelBrickWriter::operator=(const VoxelBrickWriter& rhs)
{
    if(this != &rhs)
    {
        _binary = rhs._binary;
        _compressed = rhs._compressed;
        _brickMap = rhs._brickMap;
        _storedBrickMap = rhs._storedBrickMap;
        _outputFileName = rhs._outputFileName;
        _outputPartialBricksDir = rhs._outputPartialBricksDir;
        _voxFile.swap(const_cast<VoxelBrickWriter&>(rhs)._voxFile);
    }

    return *this;
}

static bool StartTextBricksFile(const std::string& outFileName,
                               std::ofstream& voxFile)
{
    voxFile.open(outFileName, std::fstream::out);
    if(!voxFile.is_open())
    {
        return false;
    }
    
    voxFile << "<Bricks>" << std::endl;

    return true;
}

bool VoxelBrickWriter::startBricksFile(const std::string& outputDirPartialBricksDir, const std::string& fileName, bool binary, bool compressed)
{
    _binary = binary;
    _compressed = compressed;
    if(_compressed && !createOpenGLContext())
        return false;

    _outputPartialBricksDir = outputDirPartialBricksDir;
    if(mkdir(_outputPartialBricksDir.c_str()) != 0 && errno != EEXIST)
        return false;

    if(!_binary)
    {
        _outputFileName = fileName;
        _outputFileName += ".gvt";
        return StartTextBricksFile(_outputFileName, _voxFile);
    }
    else
    {
        _outputFileName = fileName;
        _outputFileName += ".gvb";
        _voxFile.open(_outputFileName, std::ios_base::out | std::ios_base::binary);
        if(!_voxFile.is_open())
            return false;

        int compressed = _compressed ? 1 : 0;
        _voxFile.write((char*)&compressed, sizeof(compressed));

        return !_voxFile.fail();
    }
}

static bool EndTextBricksFile(std::ofstream& voxFile)
{
    voxFile << "</Bricks>" << std::endl;

    voxFile.close();

    return !voxFile.fail();
}

bool VoxelBrickWriter::endBricksFile()
{
    rmdir(_outputPartialBricksDir.c_str());
    if(!_binary)
    {
        return EndTextBricksFile(_voxFile);
    }
    else
    {
        _voxFile.close();
        return !_voxFile.fail();
    }
}

void CopyPartialBrick(VoxelBrickWriter::BrickData& brickData,//brick storage
                      size_t brickX, size_t brickY, size_t brickZ,//offset in brick to start
                      size_t xOffset, size_t yOffset, size_t zOffset, //offset in mipmap to start
                      size_t xSize, size_t ySize, size_t zSize,//size of data without border
                      size_t brickDimX, size_t brickDimY,// size_t brickDimZ,//size of brick without border
                      const cuda::VoxelColors& voxelColors, //mipmap colors
                      const cuda::VoxelNormals& voxelNormals,//mipmap normals
                      size_t voxDimX, size_t voxDimY, size_t voxDimZ)//dim of mipmap
{
    size_t borderVoxels = 1u;

    size_t xEnd = xOffset + xSize;
    if(xOffset != 0u)//if it is not on left side
    {
        xOffset -= borderVoxels;
        brickX -= borderVoxels;
    }
    if(xEnd < voxDimX) //if it is not on right side
    {
        xEnd += borderVoxels;
    }
    else if(xEnd > voxDimX)
        xEnd = voxDimX;

    size_t yEnd = yOffset + ySize;
    if(yOffset != 0u)//if it is not on left side
    {
        yOffset -= borderVoxels;
        brickY -= borderVoxels;
    }
    if(yEnd < voxDimY) //if it is not on right side
    {
        yEnd += borderVoxels;
    }
    else if(yEnd > voxDimY)
        yEnd = voxDimY;

    size_t zEnd = zOffset + zSize;
    if(zOffset != 0u)//if it is not on left side
    {
        zOffset -= borderVoxels;
        brickZ -= borderVoxels;
    }
    if(zEnd < voxDimZ) //if it is not on right side
    {
        zEnd += borderVoxels;
    }
    else if(zEnd > voxDimZ)
        zEnd = voxDimZ;

    if(!brickData.brickDataStoredInMemory()
        && !brickData.loadPartialBrickFiles())
    {
        std::cerr << "Failed to open partial brick files"
                  << std::endl;
        return;
    }
    size_t brickZIndex = brickZ;
    for(size_t z = zOffset; z < zEnd; ++z, ++brickZIndex)
    {
        size_t brickYIndex = brickY;
        for(size_t y = yOffset; y < yEnd; ++y, ++brickYIndex)
        {
            const cuda::VoxColor* pReadColors = &voxelColors[(z * voxDimY * voxDimX) + (y * voxDimX) + xOffset];
            const glm::vec3* pReadNormals = &voxelNormals[(z * voxDimY * voxDimX) + (y * voxDimX) + xOffset];
            size_t writeIndex = (brickZIndex * brickDimY * brickDimX) + (brickYIndex * brickDimX) + brickX;
            memcpy(&brickData.colors[writeIndex], pReadColors, sizeof(cuda::VoxColor) * (xEnd - xOffset));
            memcpy(&brickData.normals[writeIndex], pReadNormals, sizeof(glm::vec3) * (xEnd - xOffset));
        }
    }
    
    if(!brickData.brickDataStoredInMemory()
        && !brickData.dumpMemoryToPartialBrickFiles())
    {
        std::cerr << "Failed to dump partial brick files"
                  << std::endl;
        return;
    }
    brickData.totalVoxelsStored += ((xEnd - xOffset) * (yEnd - yOffset) * (zEnd - zOffset));
}

static bool WriteTextBrick(std::ofstream& voxFile,
                           size_t xOffset, size_t yOffset, size_t zOffset,
                           size_t xSize, size_t ySize, size_t zSize,
                           size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                           const cuda::VoxelColors& voxelColors,
                           const cuda::VoxelNormals& voxelNormals)
{
    size_t borderVoxels = 1u;

    size_t brickBorderX(0u);
    size_t brickBorderY(0u);
    size_t brickBorderZ(0u);
    
    size_t xEnd = xOffset + brickDimX;
    if(xOffset != 0u && xEnd < xSize)//if it is middle brick
    {
        brickBorderX = borderVoxels;
        xEnd += borderVoxels;
        xOffset -= borderVoxels;
    }
    else if(xOffset == 0u && xEnd < xSize)//if it is left brick
    {
        xEnd += (borderVoxels << 1);
    }
    else if(xOffset != 0u) // must be right side brick
    {
        brickBorderX = borderVoxels;
        xOffset -= (borderVoxels << 1);
    }

    brickDimX = xEnd - xOffset;

    size_t yEnd = yOffset + brickDimY;
    if(yOffset != 0u && yEnd < ySize)//if it is middle brick
    {
        brickBorderY = borderVoxels;
        yEnd += borderVoxels;
        yOffset -= borderVoxels;
    }
    else if(yOffset == 0u && yEnd < ySize)//if it is left brick
    {
        yEnd += (borderVoxels << 1);
    }
    else if(yOffset != 0u) // must be right side brick
    {
        brickBorderY = borderVoxels;
        yOffset -= (borderVoxels << 1);
    }

    brickDimY = (yEnd - yOffset);

    size_t zEnd = zOffset + brickDimZ;
    if(zOffset != 0u && zEnd < zSize)//if it is middle brick
    {
        brickBorderZ = borderVoxels;
        zEnd += borderVoxels;
        zOffset -= borderVoxels;
    }
    else if(zOffset == 0u && zEnd < zSize)//if it is left brick
    {
        zEnd += (borderVoxels << 1);
    }
    else if(zOffset != 0u) // must be right side brick
    {
        brickBorderZ = borderVoxels;
        zOffset -= (borderVoxels << 1);
    }

    brickDimZ = (zEnd - zOffset);

    voxFile << " <Brick "
            << "X=\"" << xOffset << "\" "
            << "Y=\"" << yOffset << "\" "
            << "Z=\"" << zOffset << "\" "
            << "DimX=\"" << brickDimX << "\" "
            << "DimY=\"" << brickDimY << "\" "
            << "DimZ=\"" << brickDimZ << "\" "
            << "BorderX=\"" << brickBorderX << "\" "
            << "BorderY=\"" << brickBorderY << "\" "
            << "BorderZ=\"" << brickBorderZ << "\" "
            << "/>"
            << std::endl;

    voxFile << "  <BrickColors>" << std::endl;
    for(size_t z = zOffset; z < (brickDimZ + zOffset); ++z)
    {
        for(size_t y = yOffset; y < (brickDimY + yOffset); ++y)
        {
            voxFile << "    <Row Y=\"" << y << "\" Z=\"" << z << "\">" << std::endl;
            for(size_t x = xOffset; x < (brickDimX + xOffset); ++x)
            {
                const cuda::VoxColor& color = voxelColors[(z * ySize * xSize) + (y * xSize) + x];
                voxFile << "(" << color.x << "," << color.y << "," << color.z << "," << color.w << ") ";
            }
            voxFile << std::endl << "    </Row>" << std::endl;
        }
    }
    voxFile << "  </BrickColors>" << std::endl;

    voxFile << "  <BrickGradients>" << std::endl;
    for(size_t z = zOffset; z < (brickDimZ + zOffset); ++z)
    {
        for(size_t y = yOffset; y < (brickDimY + yOffset); ++y)
        {
            voxFile << "    <Row YZ=\"" << y << " " << z << "\">" << std::endl;
            for(size_t x = xOffset; x < (brickDimX + xOffset); ++x)
            {
                const glm::vec3& normal = voxelNormals[(z * ySize * xSize) + (y * xSize) + x];
                voxFile << "(" << normal.x << "," << normal.y << "," << normal.z << ") ";
            }
            voxFile << std::endl << "    </Row>" << std::endl;
        }
    }
    voxFile << "  </BrickGradients>" << std::endl;

    voxFile << " </Brick>" << std::endl;

    return !voxFile.fail();
}

class OpenGLContext 
{
public:
    OpenGLContext() {}

    bool init()
    {
        osgViewer::GraphicsWindow window;
        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        traits->x = 0;
        traits->y = 0;
        traits->width = 1;
        traits->height = 1;
        traits->windowDecoration = false;
        traits->doubleBuffer = false;
        traits->sharedContext = 0;
        traits->pbuffer = true;

        _gc = osg::GraphicsContext::createGraphicsContext(traits.get());

        if (!_gc)
        {
            //osg::notify(osg::NOTICE)<<"Failed to create pbuffer, failing back to normal graphics window."<<std::endl;
            
            traits->pbuffer = false;
            _gc = osg::GraphicsContext::createGraphicsContext(traits.get());
        }

        if (_gc.valid()) 
        {
            _gc->realize();
            _gc->makeCurrent();
            /*if (dynamic_cast<osgViewer::GraphicsWindow*>(_gc.get()))
            {
                std::cout<<"Realized graphics window for OpenGL operations."<<std::endl;
            }
            else
            {
                std::cout<<"Realized pbuffer for OpenGL operations."<<std::endl;
            }*/
            return true;
        }

        return false;
    }
    
    bool valid() const { return _gc.valid() && _gc->isRealized(); }
    
private:
    osg::ref_ptr<osg::GraphicsContext> _gc;
};

static OpenGLContext* s_pOpenGLCtx = nullptr;

bool VoxelBrickWriter::createOpenGLContext()
{
    if(s_pOpenGLCtx == nullptr)
    {
        s_pOpenGLCtx = new OpenGLContext();
        return s_pOpenGLCtx->init() && s_pOpenGLCtx->valid();
    }

    return true;
}

void VoxelBrickWriter::deleteOpenGLContext()
{
    if(s_pOpenGLCtx != nullptr)
    {
        delete s_pOpenGLCtx;
        s_pOpenGLCtx = nullptr;
    }
}

static bool CompressImage(osg::Texture3D* pTexture3D)
{
    OpenGLContext& context = *s_pOpenGLCtx;

    osg::ref_ptr<osg::State> spState = new osg::State;
        
    osg::ref_ptr<osg::Image> spImage = pTexture3D->getImage();
    if (spImage.valid() && 
        (spImage->getPixelFormat()==GL_RGB || spImage->getPixelFormat()==GL_RGBA) &&
        (spImage->s()>=4 && spImage->t()>=4 && spImage->r()>=4))
    { 
        // get OpenGL driver to create texture from spImage.
        pTexture3D->apply(*spState);

        spImage->readImageFromCurrentTexture(0, false);

        pTexture3D->setInternalFormatMode(osg::Texture::USE_IMAGE_DATA_FORMAT);

        return true;
    }

    return false;
}

static bool DeCompress(osg::Texture2D* pTexture2D)
{
    OpenGLContext& context = *s_pOpenGLCtx;

    osg::ref_ptr<osg::State> spState = new osg::State;
        
    osg::ref_ptr<osg::Image> spImage = pTexture2D->getImage();
    if (spImage.valid())
    { 
        // get OpenGL driver to create texture from spImage.
        pTexture2D->apply(*spState);

        unsigned char* pNewData = (unsigned char*)malloc(sizeof(unsigned char) * 4 
                                                         * spImage->s() 
                                                         * spImage->t());
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pNewData);

        spImage->setImage(spImage->s(), spImage->t(), 1,
                          GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE,
                          pNewData,
                          osg::Image::USE_MALLOC_FREE);

        return true;
    }

    return false;
}

//static void Scale3DImage(cuda::VoxelColors& colors,
//                         cuda::VoxelNormals& normals,
//                         glm::uvec3& curDim,
//                         size_t scaleX, size_t scaleY, size_t scaleZ,
//                         float& scalePctX, float& scalePctY, float& scalePctZ)
//{
//    cuda::VoxelColors scaledColors(scaleX * scaleY * scaleZ);
//    cuda::VoxelNormals scaledNormals(scaleX * scaleY * scaleZ);
//    
//    scalePctX = static_cast<float>(scaleX) / static_cast<float>(curDim.x);
//    scalePctY = static_cast<float>(scaleY) / static_cast<float>(curDim.y);
//    scalePctZ = static_cast<float>(scaleZ) / static_cast<float>(curDim.z);
//
//    float invScalePctX = 1.0f / scalePctX;
//    float invScalePctY = 1.0f / scalePctY;
//    float invScalePctZ = 1.0f / scalePctZ;
//
//    for(size_t z = 0; z < scaleZ; ++z)
//    {
//        size_t nearZ = static_cast<size_t>(std::floor((z * invScalePctZ) + 0.49f));
//        if(nearZ >= curDim.z)
//            nearZ = curDim.z-1;
//
//        for(size_t y = 0; y < scaleY; ++y)
//        {
//            size_t nearY = static_cast<size_t>(std::floor((y * invScalePctY) + 0.49f));
//            if(nearY >= curDim.y)
//                nearY = curDim.y-1;
//
//            for(size_t x = 0; x < scaleX; ++x)
//            {
//                size_t nearX = static_cast<size_t>(std::floor((x * invScalePctX) + 0.49f));
//                if(nearX >= curDim.x)
//                    nearX = curDim.x-1;
//
//                size_t nearIndex = (nearZ * curDim.y * curDim.x) 
//                                   + (nearY * curDim.x) 
//                                   + nearX;
//
//                scaledColors[(z * scaleY * scaleX) + (y * scaleX) + x] = 
//                    colors[nearIndex];
//
//                scaledNormals[(z * scaleY * scaleX) + (y * scaleX) + x] =
//                    normals[nearIndex];
//            }
//        }
//    }
//
//    scaledColors.swap(colors);
//    scaledNormals.swap(normals);
//
//    curDim.x = scaleX;
//    curDim.y = scaleY;
//    curDim.z = scaleZ;
//}

static bool WriteUByteBinaryBrick(std::ofstream& voxFile,
                                  size_t xOffset, size_t yOffset, size_t zOffset,
                                  size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                                  const cuda::VoxelColors& voxelColors,
                                  const cuda::VoxelNormals& voxelNormals,
                                  size_t xSize, size_t ySize)
{   
    std::vector<osg::Vec4ub> brickColorsOut;
    struct Vec3ub
    {
        unsigned char x;
        unsigned char y;
        unsigned char z;
    };
    std::vector<Vec3ub> brickNormalsOut;

    brickColorsOut.reserve(brickDimX * brickDimY * brickDimZ);
    brickNormalsOut.reserve(brickDimX * brickDimY * brickDimZ);

    for(size_t z = zOffset; z < (brickDimZ + zOffset); ++z)
    {
        for(size_t y = yOffset; y < (brickDimY + yOffset); ++y)
        {
            const cuda::VoxColor* pColorsRow = &voxelColors[(z * ySize * xSize) + (y * xSize) + xOffset];
            brickColorsOut.resize(brickColorsOut.size() + brickDimX);
                
            const glm::vec3* pNormalsRow = &voxelNormals[(z * ySize * xSize) + (y * xSize) + xOffset];
            brickNormalsOut.resize(brickNormalsOut.size() + brickDimX);
            for(size_t x = 0; x < brickDimX; ++x)
            {
                const cuda::VoxColor& color = pColorsRow[x];
                osg::Vec4ub& ubColor = brickColorsOut[brickColorsOut.size() - brickDimX + x];
                if(sizeof(color) == sizeof(ubColor))
                {
                    memcpy(&ubColor, &color, sizeof(ubColor));
                }
                else
                {
                    ubColor.r() = static_cast<unsigned char>(color.x * 255.0f);
                    ubColor.g() = static_cast<unsigned char>(color.y * 255.0f);
                    ubColor.b() = static_cast<unsigned char>(color.z * 255.0f);
                    ubColor.a() = static_cast<unsigned char>(color.w * 255.0f);
                }

                const glm::vec3& normal = pNormalsRow[x];
                Vec3ub& ubNormal = brickNormalsOut[brickNormalsOut.size() - brickDimX + x];

                if(sizeof(normal) == sizeof(ubNormal))
                {
                    memcpy(&ubNormal, &normal, sizeof(ubNormal));
                }
                else
                {
                    //map to between zero and one
                    float nX = ((normal.x + 1.0f) * 0.5f);
                    float nY = ((normal.y + 1.0f) * 0.5f);
                    float nZ = ((normal.z + 1.0f) * 0.5f);
                    ubNormal.x = static_cast<unsigned char>(nX * 255.0f);
                    ubNormal.y = static_cast<unsigned char>(nY * 255.0f);
                    ubNormal.z = static_cast<unsigned char>(nZ * 255.0f);
                }
            }
        }
    }

    int compressedImage = 0;
    voxFile.write((char*)&compressedImage, sizeof(compressedImage));
    
    unsigned int dataSize = brickColorsOut.size() * sizeof(*brickColorsOut.data());
    voxFile.write((char*)&dataSize, sizeof(dataSize));
    voxFile.write((const char*)brickColorsOut.data(), dataSize);

    voxFile.write((char*)&compressedImage, sizeof(compressedImage));

    dataSize = brickNormalsOut.size() * sizeof(*brickNormalsOut.data());
    voxFile.write((char*)&dataSize, sizeof(dataSize));
    voxFile.write((const char*)brickNormalsOut.data(), dataSize);

    return !voxFile.fail();
}

static bool WriteCompressedBinaryBrick(std::ofstream& voxFile,
                                       size_t xOffset, size_t yOffset, size_t zOffset,
                                       size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                                       const cuda::VoxelColors& voxelColors,
                                       const cuda::VoxelNormals& voxelNormals,
                                       size_t xSize, size_t ySize)
{   
    cuda::VoxelColors brickColorsOut;
    cuda::VoxelNormals brickNormalsOut;

    brickColorsOut.reserve(brickDimX * brickDimY * brickDimZ);
    brickNormalsOut.reserve(brickDimX * brickDimY * brickDimZ);

    for(size_t z = zOffset; z < (brickDimZ + zOffset); ++z)
    {
        for(size_t y = yOffset; y < (brickDimY + yOffset); ++y)
        {
            const cuda::VoxColor* pColorsRow = &voxelColors[(z * ySize * xSize) + (y * xSize) + xOffset];
            brickColorsOut.insert(brickColorsOut.end(), pColorsRow, pColorsRow + brickDimX);
                
            const glm::vec3* pNormalsRow = &voxelNormals[(z * ySize * xSize) + (y * xSize) + xOffset];
            brickNormalsOut.insert(brickNormalsOut.end(), pNormalsRow, pNormalsRow + brickDimX);
            for(size_t x = 0; x < brickDimX; ++x)
            {
                glm::vec3& normal = brickNormalsOut[brickNormalsOut.size() - brickDimX + x];
                //map to between zero and one
                float nX = ((normal.x + 1.0f) * 0.5f);
                float nY = ((normal.y + 1.0f) * 0.5f);
                float nZ = ((normal.z + 1.0f) * 0.5f);
                normal.x = nX;
                normal.y = nY;
                normal.z = nZ;
            }
        }
    }

    if(brickColorsOut.size() > 0)
    {
        osg::ref_ptr<osg::Image> spImage = new osg::Image();
        if(sizeof(cuda::VoxColor) == 4)
        {
            spImage->setImage(brickDimX, brickDimY, brickDimZ, 
                              GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 
                              (unsigned char*)&brickColorsOut.front(), osg::Image::NO_DELETE);
        }
        else
        {
            spImage->setImage(brickDimX, brickDimY, brickDimZ, 
                              GL_RGBA32F_ARB, GL_RGBA, GL_FLOAT, 
                              (unsigned char*)&brickColorsOut.front(), osg::Image::NO_DELETE);
        }

        osg::ref_ptr<osg::Texture3D> spTexture = new osg::Texture3D(spImage);
        
        spTexture->setInternalFormatMode(osg::Texture::USE_S3TC_DXT5_COMPRESSION);

        spTexture->setResizeNonPowerOfTwoHint(false);
        spTexture->setUnRefImageDataAfterApply(false);
        //no need to generate mip maps
        spTexture->setUseHardwareMipMapGeneration(false);
        //this prevents cpu side mip map
        spTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);

        unsigned int before = spImage->getTotalSizeInBytes();

        if(!CompressImage(spTexture.get()))
            return false;

        int compressedImage = 1;
        if(spImage->getInternalTextureFormat() != GL_COMPRESSED_RGBA_S3TC_DXT5_EXT)
        {
            std::cerr << "Brick colors compression failed " 
                      << GL_COMPRESSED_RGBA_S3TC_DXT5_EXT << " != " 
                      << spImage->getInternalTextureFormat() << std::endl;
            compressedImage = 0;
        }

        unsigned int after = spImage->getTotalSizeInBytes();

        voxFile.write((char*)&compressedImage, sizeof(compressedImage));
        voxFile.write((char*)&after, sizeof(after));
        voxFile.write((const char*)spImage->data(), after);
    }

    if(brickNormalsOut.size() > 0)
    {
        osg::ref_ptr<osg::Image> spImage = new osg::Image();
        if(sizeof(cuda::VoxNorm) == 4)
        {
            spImage->setImage(brickDimX, brickDimY, brickDimZ, 
                              GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 
                              (unsigned char*)&brickNormalsOut.front(), osg::Image::NO_DELETE);
        }
        else
        {
            spImage->setImage(brickDimX, brickDimY, brickDimZ, 
                              GL_RGB32F_ARB, GL_RGB, GL_FLOAT, 
                              (unsigned char*)&brickNormalsOut.front(), osg::Image::NO_DELETE);
        }

        osg::ref_ptr<osg::Texture3D> spTexture = new osg::Texture3D(spImage);
        
        spTexture->setInternalFormatMode(osg::Texture::USE_S3TC_DXT1_COMPRESSION);

        spTexture->setResizeNonPowerOfTwoHint(false);
        spTexture->setUnRefImageDataAfterApply(false);
        //no need to generate mip maps
        spTexture->setUseHardwareMipMapGeneration(false);
        //this prevents cpu side mip maps
        spTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);

        unsigned int before = spImage->getTotalSizeInBytes();

        if(!CompressImage(spTexture.get()))
            return false;

        int compressedImage = 1;
        if(spImage->getInternalTextureFormat() != GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
        {
            std::cerr << "Brick gradient compression failed " 
                      << GL_COMPRESSED_RGB_S3TC_DXT1_EXT 
                      << " != " << spImage->getInternalTextureFormat() << std::endl;
            compressedImage = 0;
        }

        unsigned int after = spImage->getTotalSizeInBytes();
        
        voxFile.write((char*)&compressedImage, sizeof(compressedImage));
        voxFile.write((char*)&after, sizeof(after));
        voxFile.write((const char*)spImage->data(), after);
    }

    return !voxFile.fail();
}

static bool WriteBinaryBrick(std::ofstream& voxFile,
                             bool compressed,
                             size_t xOffset, size_t yOffset, size_t zOffset,
                             size_t xSize, size_t ySize, size_t zSize,
                             size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                             const cuda::VoxelColors& voxelColors,
                             const cuda::VoxelNormals& voxelNormals)
{
    //bricks must contain border brick data  for correct interpolation.
    unsigned int borderVoxels = 1u;

    unsigned int brickBorderX(0u);
    unsigned int brickBorderY(0u);
    unsigned int brickBorderZ(0u);
    
    size_t xEnd = xOffset + brickDimX;
    if(xOffset != 0u && xEnd < xSize)//if it is middle brick
    {
        brickBorderX = borderVoxels;
        xEnd += borderVoxels;
        xOffset -= borderVoxels;
    }
    else if(xOffset == 0u && xEnd < xSize)//if it is left brick
    {
        xEnd += (borderVoxels << 1);
    }
    else if(xOffset != 0u) // must be right side brick
    {
        brickBorderX = borderVoxels;
        xOffset -= (borderVoxels << 1);
    }

    brickDimX = (xEnd - xOffset);

    size_t yEnd = yOffset + brickDimY;
    if(yOffset != 0u && yEnd < ySize)//if it is middle brick
    {
        brickBorderY = borderVoxels;
        yEnd += borderVoxels;
        yOffset -= borderVoxels;
    }
    else if(yOffset == 0u && yEnd < ySize)//if it is left brick
    {
        yEnd += (borderVoxels << 1);
    }
    else if(yOffset != 0u) // must be right side brick
    {
        brickBorderY = borderVoxels;
        yOffset -= (borderVoxels << 1);
    }

    brickDimY = (yEnd - yOffset);

    size_t zEnd = zOffset + brickDimZ;
    if(zOffset != 0u && zEnd < zSize)//if it is middle brick
    {
        brickBorderZ = borderVoxels;
        zEnd += borderVoxels;
        zOffset -= borderVoxels;
    }
    else if(zOffset == 0u && zEnd < zSize)//if it is left brick
    {
        zEnd += (borderVoxels << 1);
    }
    else if(zOffset != 0u) // must be right side brick
    {
        brickBorderZ = borderVoxels;
        zOffset -= (borderVoxels << 1);
    }

    brickDimZ = (zEnd - zOffset);

    //write brick dims
    voxFile.write((char*)&brickDimX, sizeof(brickDimX));
    voxFile.write((char*)&brickDimY, sizeof(brickDimY));
    voxFile.write((char*)&brickDimZ, sizeof(brickDimZ));
    //write the border
    voxFile.write((char*)&brickBorderX, sizeof(brickBorderX));
    voxFile.write((char*)&brickBorderY, sizeof(brickBorderY));
    voxFile.write((char*)&brickBorderZ, sizeof(brickBorderZ));

    if(compressed)
        return WriteCompressedBinaryBrick(voxFile, 
                                          xOffset, yOffset, zOffset,
                                          brickDimX, brickDimY, brickDimZ,
                                          voxelColors, voxelNormals,
                                          xSize, ySize);
    else
        return WriteUByteBinaryBrick(voxFile,
                                     xOffset, yOffset, zOffset,
                                     brickDimX, brickDimY, brickDimZ,
                                     voxelColors, voxelNormals,
                                     xSize, ySize);
}

bool VoxelBrickWriter::writeBrick(size_t nodeX, size_t nodeY, size_t nodeZ,
                                  size_t xOffset, size_t yOffset, size_t zOffset,
                                  size_t xSize, size_t ySize, size_t zSize,
                                  size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                                  const cuda::VoxelColors& voxelColors,
                                  const cuda::VoxelNormals& voxelNormals)
{
    BrickID brickID(nodeX, nodeY, nodeZ);
#ifdef __DEBUG__
    size_t before = _brickMap.size();
#endif
    _brickMap[brickID] = _brickMap.size();
#ifdef __DEBUG__
    size_t after = _brickMap.size();
    if(before == after)
        return false;
#endif

    if(!_binary)
    {
        return WriteTextBrick(_voxFile,
                              xOffset, yOffset, zOffset,
                              xSize, ySize, zSize,
                              brickDimX, brickDimY, brickDimZ,
                              voxelColors,
                              voxelNormals);
    }
    else
    {
        return WriteBinaryBrick(_voxFile,
                                _compressed,
                                xOffset, yOffset, zOffset,
                                xSize, ySize, zSize,
                                brickDimX, brickDimY, brickDimZ,
                                voxelColors,
                                voxelNormals);
    }
}

static size_t s_totalPartialBricksAllocatedBytes = 0;
static size_t s_maxPartialBricksAllocatedBytes = 1200000000;//1.75GB

VoxelBrickWriter::BrickData::~BrickData()
{
    if(this->colors.size() > 0)
        s_totalPartialBricksAllocatedBytes -= (this->colors.size() * sizeof(cuda::VoxColor));
    if(this->normals.size() > 0)
        s_totalPartialBricksAllocatedBytes -= (this->normals.size() * sizeof(glm::vec3));
}

bool VoxelBrickWriter::BrickData::init(const std::string& partialBrickDataDir,
                                       size_t nodeX, size_t nodeY, size_t nodeZ)
{
    if(s_totalPartialBricksAllocatedBytes < s_maxPartialBricksAllocatedBytes)
    {
        this->storeBrickDataInFiles = false;
        this->colors.resize(this->brickDimX *
                            this->brickDimY *
                            this->brickDimZ);

        this->normals.resize(this->brickDimX *
                             this->brickDimY *
                             this->brickDimZ);

        s_totalPartialBricksAllocatedBytes += (this->colors.size() * sizeof(cuda::VoxColor));
        s_totalPartialBricksAllocatedBytes += (this->normals.size() * sizeof(glm::vec3));
    }
    else
    {
        this->storeBrickDataInFiles = true;
        std::stringstream colorsFilePath;
        colorsFilePath << partialBrickDataDir << "/colors" << nodeX << "_" << nodeY << "_" << nodeZ << ".voxb";
    
        partialBrickColorsFilePath = colorsFilePath.str();

        std::ofstream partialBrickColorsFile(partialBrickColorsFilePath,
                                             std::ios_base::out | std::ios_base::binary);

        size_t fileSize = sizeof(cuda::VoxColor) * 
                            (this->brickDimX *
                             this->brickDimY *
                             this->brickDimZ);
        int zero = 0;
        partialBrickColorsFile.seekp(fileSize - sizeof(zero));
        partialBrickColorsFile.write((char*)&zero, sizeof(zero));
        
        std::stringstream normalsFilePath;
        normalsFilePath << partialBrickDataDir << "/normals" << nodeX << "_" << nodeY << "_" << nodeZ << ".voxb";
    
        partialBrickNormalsFilePath = normalsFilePath.str();

        std::ofstream partialBrickNormalsFile(partialBrickNormalsFilePath,
                                              std::ios_base::out | std::ios_base::binary);

        fileSize = sizeof(glm::vec3) * 
                            (this->brickDimX *
                             this->brickDimY *
                             this->brickDimZ);

        partialBrickNormalsFile.seekp(fileSize - sizeof(zero));
        partialBrickNormalsFile.write((char*)&zero, sizeof(zero));

        if(partialBrickColorsFile.is_open() == false
            || partialBrickNormalsFile.is_open() == false
            || partialBrickColorsFile.fail()
            || partialBrickColorsFile.bad()
            || partialBrickNormalsFile.fail()
            || partialBrickNormalsFile.bad())
        {
            return false;
        }
    }

    return true;
}

bool VoxelBrickWriter::BrickData::dumpMemoryToPartialBrickFiles()
{
    std::ofstream partialBrickColorsFile(partialBrickColorsFilePath,
                                         std::ios_base::in | std::ios_base::out | std::ios_base::binary);

    size_t dataSize = sizeof(cuda::VoxColor) * 
                        (this->brickDimX *
                        this->brickDimY *
                        this->brickDimZ);

    partialBrickColorsFile.write((const char*)this->colors.data(),
                                        dataSize);
    {
        //free memory
        s_totalPartialBricksAllocatedBytes -= (this->colors.size() * sizeof(cuda::VoxColor));
        cuda::VoxelColors empty;
        empty.swap(this->colors);
    }

    std::ofstream partialBrickNormalsFile(partialBrickNormalsFilePath,
                                          std::ios_base::in | std::ios_base::out | std::ios_base::binary);
   
    dataSize = sizeof(glm::vec3) * 
                        (this->brickDimX *
                        this->brickDimY *
                        this->brickDimZ);

    partialBrickColorsFile.write((const char*)this->normals.data(),
                                        dataSize);

    {
        s_totalPartialBricksAllocatedBytes -= (this->normals.size() * sizeof(glm::vec3));
        //free memory
        cuda::VoxelNormals empty;
        empty.swap(this->normals);
    }

    if(partialBrickColorsFile.is_open() == false
        || partialBrickNormalsFile.is_open() == false
        || partialBrickColorsFile.fail()
        || partialBrickColorsFile.bad()
        || partialBrickNormalsFile.fail()
        || partialBrickNormalsFile.bad())
    {
        partialBrickNormalsFile.close();
        partialBrickColorsFile.close();
        return false;
    }

    partialBrickNormalsFile.close();
    partialBrickColorsFile.close();

    return true;
}

bool VoxelBrickWriter::BrickData::loadPartialBrickFiles()
{
    this->colors.resize(this->brickDimX *
                        this->brickDimY *
                        this->brickDimZ);
    s_totalPartialBricksAllocatedBytes += (this->colors.size() * sizeof(cuda::VoxColor));

    std::ifstream colorsFile;
    colorsFile.open(partialBrickColorsFilePath.c_str(),
                    std::ios_base::in | std::ios_base::binary);
    if(!colorsFile.is_open())
        return false;

    colorsFile.read((char*)this->colors.data(),
                    colors.size() * sizeof(cuda::VoxColor));
    colorsFile.close();

    this->normals.resize(this->brickDimX *
                         this->brickDimY *
                         this->brickDimZ);
    s_totalPartialBricksAllocatedBytes += (this->normals.size() * sizeof(glm::vec3));

    std::ifstream normalsFile;
    normalsFile.open(partialBrickNormalsFilePath.c_str(),
                     std::ios_base::in | std::ios_base::binary);
    if(!normalsFile.is_open())
        return false;

    normalsFile.read((char*)normals.data(),
                     normals.size() * sizeof(glm::vec3));
    normalsFile.close();

    return true;
}

bool VoxelBrickWriter::storePartialBrick(size_t nodeX, size_t nodeY, size_t nodeZ,
                                         size_t brickX, size_t brickY, size_t brickZ,
                                         size_t xOffset, size_t yOffset, size_t zOffset,
                                         size_t xSize, size_t ySize, size_t zSize,
                                         size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                                         const cuda::VoxelColors& voxelColors,
                                         const cuda::VoxelNormals& voxelNormals,
                                         size_t voxDimX, size_t voxDimY, size_t voxDimZ)
{
    BrickID brickID(nodeX, nodeY, nodeZ);

    BrickDataMap::iterator brickDataItr = _storedBrickMap.find(brickID);
    if(brickDataItr == _storedBrickMap.end())
    {
        size_t borderVoxels(2u);
        std::pair<BrickDataMap::iterator, bool> ret = 
            _storedBrickMap.insert(std::make_pair(brickID, 
                                                  BrickData(brickDimX + borderVoxels, 
                                                            brickDimY + borderVoxels, 
                                                            brickDimZ + borderVoxels)));
        if(ret.second == false)
            return false;
        brickDataItr = ret.first;
        if(!brickDataItr->second.init(_outputPartialBricksDir,
                                  nodeX, nodeY, nodeZ))
            return false;                             
    }

    CopyPartialBrick(brickDataItr->second,
                     brickX, brickY, brickZ,
                     xOffset, yOffset, zOffset,
                     xSize, ySize, zSize,
                     brickDataItr->second.brickDimX,
                     brickDataItr->second.brickDimY,
                     //brickDataItr->second.brickDimZ,
                     voxelColors, voxelNormals,
                     voxDimX, voxDimY, voxDimZ);
    return true;
}

static inline bool VEC4_EQUAL(const glm::vec4& v1, const glm::vec4& v2)
{
    static float epsilon = 0.001f;
    glm::vec4 diff = v1 - v2;

    return (diff.x > -epsilon && diff.x < epsilon) &&
           (diff.y > -epsilon && diff.y < epsilon) &&
           (diff.z > -epsilon && diff.z < epsilon) &&
           (diff.w > -epsilon && diff.w < epsilon);
}

static inline bool VEC4_EQUAL(const uchar4& v1, const uchar4& v2)
{
    return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z && v1.w == v2.w;
}

static bool ValidateNode(size_t xOffset, size_t yOffset, size_t zOffset,
                         size_t xSize, size_t ySize, size_t zSize,
                         const glm::uvec3& brickDim,
                         const cuda::VoxelColors& voxelColors,
                         glm::uint type)
{
    size_t startX = xOffset;
    if(xOffset != 0)
        startX -= 1;

    size_t endX = xOffset + brickDim.x;
    if(endX != xSize)
        endX += 1;

    size_t startY = yOffset;
    if(yOffset != 0)
        startY -= 1;

    size_t endY = yOffset + brickDim.y;
    if(endY != ySize)
        endY += 1;

    size_t startZ = zOffset;
    if(zOffset != 0)
        startZ -= 1;

    size_t endZ = zOffset + brickDim.z;
    if(endZ != zSize)
        endZ += 1;

    const cuda::VoxColor& constColor = voxelColors[(zOffset * xSize * ySize) + (yOffset * xSize) + xOffset];
    for(size_t z = startZ; z < endZ; ++z)
    {
        for(size_t y = startY; y < endY; ++y)
        {
            for(size_t x = startX; x < endX; ++x)
            {
                const cuda::VoxColor& curColor = voxelColors[(z * xSize * ySize) + (y * xSize) + x];
                if(VEC4_EQUAL(curColor, constColor) == false)
                {
                    return type == 1u;
                }
            }
        }
    }

    return type == 0u || type == 2u;
}

bool VoxelBrickWriter::writeCompletedStoredBricks(const unsigned int* pOctTreeNodes,
                                         size_t octTreeDimX,
                                         size_t octTreeDimY)
{
    bool retVal = true;
    std::vector<BrickID> completedBricks;
    for(BrickDataMap::iterator itr = _storedBrickMap.begin();
        itr != _storedBrickMap.end();
        ++itr)
    {
        size_t octTreeNodeIndex = (itr->first.z * octTreeDimY * octTreeDimX)
                                + (itr->first.y * octTreeDimY)
                                + itr->first.x;
        if(itr->second.isComplete())
        {
#ifdef __DEBUG__
            if(!itr->second.brickDataStoredInMemory())
            {
                if(!itr->second.loadPartialBrickFiles())
                {
                    std::cerr << "Failed to load partial brick files." << std::endl;
                }
            }

            if(!ValidateNode(1, 1, 1,
                           itr->second.brickDimX,
                           itr->second.brickDimY,
                           itr->second.brickDimZ,
                           glm::uvec3(itr->second.brickDimX - 2u,
                           itr->second.brickDimY - 2u,
                           itr->second.brickDimZ - 2u),
                           itr->second.colors,
                           pOctTreeNodes[octTreeNodeIndex]))
            {
                 std::cerr << "Failed to validate node." << std::endl;
            }
#endif
            if(pOctTreeNodes[octTreeNodeIndex] == 1u)
            {
#ifndef __DEBUG__
                if(!itr->second.brickDataStoredInMemory())
                {
                    if(!itr->second.loadPartialBrickFiles())
                    {
                        std::cerr << "Failed to load partial brick files." << std::endl;
                    }
                }
#endif
                if(!writeBrick(itr->first.x, itr->first.y, itr->first.z,
                                1, 1, 1,
                                itr->second.brickDimX,
                                itr->second.brickDimY,
                                itr->second.brickDimZ,
                                itr->second.brickDimX - 2u,
                                itr->second.brickDimY - 2u,
                                itr->second.brickDimZ - 2u,
                                itr->second.colors,
                                itr->second.normals))
                {
                    std::cerr << "Error writing stored brick." << std::endl;
                    retVal = false;
                }
            }
            std::remove(itr->second.partialBrickColorsFilePath.c_str());
            std::remove(itr->second.partialBrickNormalsFilePath.c_str());
            completedBricks.push_back(itr->first);
        }
    }

    //erase any bricks that have been completed 
    //(i.e. all voxels have been examined and stored)
    for(std::vector<BrickID>::iterator itr = completedBricks.begin();
        itr != completedBricks.end();
        ++itr)
    {
        _storedBrickMap.erase(*itr);
    }

    return retVal;
}

bool VoxelBrickWriter::ConvertImageToRGBA8(osg::Image* pImage)
{
    osg::ref_ptr<osg::Texture2D> spTexture = new osg::Texture2D(pImage);
    spTexture->setInternalFormatMode(osg::Texture::USE_IMAGE_DATA_FORMAT);
    spTexture->setResizeNonPowerOfTwoHint(false);
    spTexture->setUnRefImageDataAfterApply(false);
    //no need to generate mip maps
    spTexture->setUseHardwareMipMapGeneration(false);
    //this prevents cpu side mip maps
    spTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);

    unsigned int before = pImage->getTotalSizeInBytes();

    if(!DeCompress(spTexture.get()))
        return false;

    unsigned int after = pImage->getTotalSizeInBytes();
    GLint fmt = pImage->getInternalTextureFormat();
    return fmt == GL_RGBA8;
}

bool VoxelBrickWriter::ExportToFileHeader(size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                          const std::string& headerFileName,
                                          bool formatIsUByte,
                                          const std::vector<std::string>& voxelColorFiles,
                                          const std::vector<glm::uvec3>& voxelColorStartRanges,
                                          const std::vector<glm::uvec3>& voxelColorEndRanges)
{
    std::ofstream voxFile;
    voxFile.open(headerFileName, std::fstream::out);
    if(!voxFile.is_open())
    {
        return false;
    }

    voxFile << "VOXEL_HEADER" << std::endl;
    voxFile << "POSITION " 
            << -1.0f * static_cast<float>(voxDimX >> 1) << " "
            << -1.0f * static_cast<float>(voxDimY >> 1) << " "
            << -1.0f * static_cast<float>(voxDimZ >> 1) << std::endl;
    voxFile << "ORIENTATION 0 0 1 0" << std::endl;
    voxFile << "SCALE 1" << std::endl;
    voxFile << "DIMENSIONS "
            << voxDimX << " "
            << voxDimY << " "
            << voxDimZ << std::endl;
    voxFile << "TYPE COLORS" << std::endl;
    voxFile << "FORMAT " << (formatIsUByte ? "GL_RGBA8" : "GL_RGBF32") << std::endl;
    voxFile << "VOXEL_HEADER_END" << std::endl;
    voxFile << "VOXEL_SUB_IMAGE_FILES" << std::endl;
    for(size_t i = 0; i < voxelColorFiles.size(); ++i)
    {
        const std::string& voxelBinaryFile = voxelColorFiles.at(i);
        const glm::uvec3& subRangeStart = voxelColorStartRanges.at(i);
        const glm::uvec3& subRangeEnd = voxelColorEndRanges.at(i);

        voxFile << "VOXEL_SUB_IMAGE_FILE " 
                << subRangeStart.x << " " << subRangeStart.y << " " << subRangeStart.z << " "
                << subRangeEnd.x << " " << subRangeEnd.y << " " << subRangeEnd.z << " "
                << voxelBinaryFile
                << std::endl;
    }
    voxFile.close();

    return true;
}

bool VoxelBrickWriter::ExportToFile(const cuda::VoxelColors& voxelColors,
                                    const cuda::VoxelNormals& voxelNormals,
                                    bool convertToUByte,
                                    size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                    size_t xStart, size_t yStart, size_t zStart,
                                    size_t xEnd, size_t yEnd, size_t zEnd,
                                    const std::string& outputFileName)
{    
    std::ofstream voxColors;
    voxColors.open(outputFileName, std::ios_base::out | std::ios_base::binary);
    if(voxColors.is_open() == false)
        return false;
    int formatIsUByte = convertToUByte ? 1 : 0;
    voxColors.write((const char*)&formatIsUByte, sizeof(formatIsUByte));
    size_t xSize = xEnd - xStart;
    size_t ySize = yEnd - yStart;
    size_t zSize = zEnd - zStart;
    unsigned int dataSize;
    if(convertToUByte)
        dataSize = sizeof(osg::Vec4ub)
                      * xSize * ySize * zSize;
    else
        dataSize = sizeof(cuda::VoxelColors::value_type)
                      * xSize * ySize * zSize;
    voxColors.write((const char*)&dataSize, sizeof(dataSize));
    for(size_t z = zStart; z < zEnd; ++z)
    {
        for(size_t y = yStart; y < yEnd; ++y)
        {
            if(!convertToUByte || sizeof(cuda::VoxelColors::value_type) == 4)
            {
                size_t rowSize = xSize * sizeof(cuda::VoxelColors::value_type);
                size_t index = (z * voxDimX * voxDimY) + (y * voxDimX) + xStart;
                voxColors.write((const char*)(&voxelColors[index]),
                                rowSize);
            }
            else
            {
                std::vector<osg::Vec4ub> colorsRow;
                colorsRow.reserve(xSize);
                for(size_t x = xStart; x < xEnd; ++x)
                {
                    colorsRow.resize(colorsRow.size() + 1);
                    osg::Vec4ub& color = colorsRow.back();
                    size_t index = (z * voxDimX * voxDimY) + (y * voxDimX) + x;
                    const cuda::VoxelColors::value_type& fColor = voxelColors.at(index);
                    color.r() = static_cast<unsigned char>(fColor.x * 255.0f);
                    color.g() = static_cast<unsigned char>(fColor.y * 255.0f);
                    color.b() = static_cast<unsigned char>(fColor.z * 255.0f);
                    color.a() = static_cast<unsigned char>(fColor.w * 255.0f);
                }
                voxColors.write((const char*)&colorsRow[0], sizeof(osg::Vec4ub) * xSize);
            }
        }
    }
    voxColors.close();
    
    return true;
}