#ifndef CUDA_VOXEL_BRICK_WRITER_H
#define CUDA_VOXEL_BRICK_WRITER_H

#include "VoxelizerTypes.h"

#include <vector>
#include <map>
#include <fstream>

namespace osg
{
    class Image; //forward declaration
};

namespace cuda
{
    class VoxelBrickWriter
    {
    public:
        struct BrickID
        {
            size_t x; 
            size_t y; 
            size_t z;
            BrickID(size_t _x, size_t _y, size_t _z) : x(_x), y(_y), z(_z) {}
            bool operator<(const BrickID& rhs) const
            {
                return z < rhs.z || 
                      (z == rhs.z && y < rhs.y) || 
                      (z == rhs.z && y == rhs.y && x < rhs.x);
            }
        };
        typedef std::map<BrickID, size_t> BrickMap;
        struct BrickData
        {
            BrickData(size_t xSize, size_t ySize, size_t zSize) : 
                storeBrickDataInFiles(false),
                brickDimX(xSize), brickDimY(ySize), brickDimZ(zSize),
                totalVoxelsStored(0) {}
            BrickData(const BrickData& copy) :
                storeBrickDataInFiles(copy.storeBrickDataInFiles),
                partialBrickColorsFilePath(copy.partialBrickColorsFilePath),
                partialBrickNormalsFilePath(copy.partialBrickNormalsFilePath),
                brickDimX(copy.brickDimX),
                brickDimY(copy.brickDimY),
                brickDimZ(copy.brickDimZ),
                totalVoxelsStored(copy.totalVoxelsStored)
            {
                this->colors.swap(const_cast<BrickData&>(copy).colors);
                this->normals.swap(const_cast<BrickData&>(copy).normals);
            }

            ~BrickData();

            BrickData& operator=(const BrickData& rhs)
            {
                if(this != &rhs)
                {
                    this->storeBrickDataInFiles = rhs.storeBrickDataInFiles;
                    this->colors.swap(const_cast<BrickData&>(rhs).colors);
                    this->normals.swap(const_cast<BrickData&>(rhs).normals);
                    this->partialBrickColorsFilePath = rhs.partialBrickColorsFilePath;
                    this->partialBrickNormalsFilePath = rhs.partialBrickNormalsFilePath;
                    this->brickDimX = rhs.brickDimX;
                    this->brickDimY = rhs.brickDimY;
                    this->brickDimZ = rhs.brickDimZ;
                    this->totalVoxelsStored = rhs.totalVoxelsStored;
                }
                return *this;
            }

            bool init(const std::string& partialBrickDir,
                      size_t nodeX, size_t nodeY, size_t nodeZ);

            bool dumpMemoryToPartialBrickFiles();
            bool loadPartialBrickFiles();

            bool isComplete() const
            { 
                return totalVoxelsStored == (brickDimX * brickDimY * brickDimZ);
            }

            bool brickDataStoredInMemory()
            {
                return !this->storeBrickDataInFiles;
            }

            bool storeBrickDataInFiles;
            
            cuda::VoxelColors colors;
            std::string partialBrickColorsFilePath;
            
            cuda::VoxelNormals normals;
            std::string partialBrickNormalsFilePath;
            
            size_t brickDimX;
            size_t brickDimY;
            size_t brickDimZ;
            size_t totalVoxelsStored;
        };
        typedef std::map<BrickID, BrickData> BrickDataMap;
    private:
        bool _binary;
        bool _compressed;        
        BrickMap _brickMap;
        BrickDataMap _storedBrickMap;
        std::ofstream _voxFile;
        std::string _outputFileName;
        std::string _outputPartialBricksDir;
    public:
        VoxelBrickWriter();
        VoxelBrickWriter(const VoxelBrickWriter& copy);
        ~VoxelBrickWriter();
        VoxelBrickWriter& operator=(const VoxelBrickWriter& rhs);

        bool createOpenGLContext();
        void deleteOpenGLContext();

        bool startBricksFile(const std::string& outputDirPartialBricksDir, const std::string& fileName, bool binary, bool compressed);

        bool writeBrick(size_t nodeX, size_t nodeY, size_t nodeZ,
                        size_t xOffset, size_t yOffset, size_t zOffset,
                        size_t xSize, size_t ySize, size_t zSize,
                        size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                        const cuda::VoxelColors& voxelColors,
                        const cuda::VoxelNormals& voxelNormals);

        bool storePartialBrick(size_t nodeX, size_t nodeY, size_t nodeZ,
                               size_t brickX, size_t brickY, size_t brickZ,
                               size_t xOffset, size_t yOffset, size_t zOffset,
                               size_t xSize, size_t ySize, size_t zSize,
                               size_t brickDimX, size_t brickDimY, size_t brickDimZ,
                               const cuda::VoxelColors& voxelColors,
                               const cuda::VoxelNormals& voxelNormals,
                               size_t voxDimX, size_t voxDimY, size_t voxDimZ);

        bool writeCompletedStoredBricks(const unsigned int* pOctTreeNodes,
                               size_t octTreeDimX,
                               size_t octTreeDimY);//TODO only write bricks that are non-const

        bool endBricksFile();

        size_t getBrickIndex(size_t x, size_t y, size_t z) const
        {
            BrickID brickID(x, y, z);
            BrickMap::const_iterator findIt = _brickMap.find(brickID);
            if(findIt == _brickMap.end())
                return 0;
            return findIt->second;
        }

        static bool ConvertImageToRGBA8(osg::Image* pImage);
        
        static bool ExportToFileHeader(size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                const std::string& headerFileName,
                                bool formatIsUByte,
                                const std::vector<std::string>& voxelColorFiles,
                                const std::vector<glm::uvec3>& voxelColorStartRanges,
                                const std::vector<glm::uvec3>& voxelColorEndRanges);

        static bool ExportToFile(const cuda::VoxelColors& voxelColors,
                                 const cuda::VoxelNormals& voxelNormals,
                                 bool convertToUByte,
                                 size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                 size_t xStart, size_t yStart, size_t zStart,
                                 size_t xEnd, size_t yEnd, size_t zEnd,
                                 const std::string& outputFileName);
    private:
        size_t getBrickCount() const { return _brickMap.size(); }

        const std::string& getOutputFileName() const { return _outputFileName; }
    };

    typedef std::vector<VoxelBrickWriter> VoxelBrickWriters;
}

#endif