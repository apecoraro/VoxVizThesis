#ifndef CUDA_VOXELIZER_HH
#define CUDA_VOXELIZER_HH

#include <cuda_runtime.h>

#include <glm/glm.hpp>
#include "VoxelizerTypes.h"
#include "VoxelBrickWriter.h"

#include <sstream>
#include <vector>
#include <map>

namespace cuda
{
    struct VoxelBBox
    {
        glm::uvec3 minVox;
        glm::uvec3 maxVox;
    };

    class Voxelizer
    {
    public:
        typedef std::pair<cudaPitchedPtr, cudaPitchedPtr> VoxelDataPtrs;
        typedef std::vector<VoxelDataPtrs> VoxelDeviceMipMaps;
        struct OctTreeNodeDevice
        {
            glm::uint* pTypes;
            cuda::VoxColor* pConstColors;
            OctTreeNodeDevice() : pTypes(0), pConstColors(0) {}
        };
        typedef std::vector<OctTreeNodeDevice> OctTreeDeviceBuffers;

        typedef glm::uint OctTreeNode;
        typedef std::vector<OctTreeNode> OctTreeNodes;
        typedef std::vector<cuda::VoxColor> OctTreeConstColors;
    private:
        glm::vec3* _pgVerts;
        glm::vec3* _pgEdges;
        glm::vec3* _pgFaceNormals;
        glm::vec3* _pgVtxNormals;
        VoxelBBox* _pgBounds;
        glm::vec2* _pgUVs;
        typedef std::map<unsigned char*, cudaArray*> ImageMap;
        ImageMap _imageMap;

        int _maxThreadsPerBlock;
        glm::ivec3 _maxBlockDim;

        struct TriangleGroup
        {
            const std::vector<glm::vec3>* pVerts;
            const std::vector<glm::vec3>* pVertNormals;
            const std::vector<glm::vec2>* pUVs;
            VoxelBBox voxelBBox;
            bool isTerrain;
            unsigned char* pImageData;
            cudaChannelFormatDesc imageDesc;
            int imageWidth;
            int imageHeight;
            cudaTextureAddressMode texAddressMode0;
            cudaTextureAddressMode texAddressMode1;
            TriangleGroup(const std::vector<glm::vec3>* _pVerts,
                          const std::vector<glm::vec3>* _pVertNormals,
                          const std::vector<glm::vec2>* _pUVs,
                          const glm::uvec3& minVox,
                          const glm::uvec3& maxVox,
                          bool _isTerrain,
                          unsigned char* _pImageData,
                          const cudaChannelFormatDesc& _imageDesc,
                          int _imageWidth,
                          int _imageHeight,
                          cudaTextureAddressMode _texAddressMode0,
                          cudaTextureAddressMode _texAddressMode1) :
                pVerts(_pVerts),
                pVertNormals(_pVertNormals),
                pUVs(_pUVs),
                isTerrain(_isTerrain),
                pImageData(_pImageData),
                imageDesc(_imageDesc),
                imageWidth(_imageWidth),
                imageHeight(_imageHeight),
                texAddressMode0(_texAddressMode0),
                texAddressMode1(_texAddressMode1)
                {
                    voxelBBox.minVox = minVox;
                    voxelBBox.maxVox = maxVox;
                }
        };
        typedef std::vector<TriangleGroup> TriangleGroups;
        TriangleGroups _triangleGroups;
        size_t _totalVertCount;
        size_t _totalVertNormalsCount;
        size_t _totalUVCount;
        glm::uvec3 _voxDim;
        glm::uvec3 _brickDim;
        glm::uvec3 _extraVoxChunk;
        glm::uvec3 _voxChunkDim;
        glm::vec3 _p;
        glm::vec3 _offsetP;
        glm::vec3 _deltaP;
        glm::uint _numMipMapLevels;

        //voxel gpu memory
        VoxelDeviceMipMaps _voxelDeviceMipMaps;
        cudaPitchedPtr _voxelTriIndicesDevPtr;
        cudaPitchedPtr _voxelTriCountsDevPtr;
        cudaPitchedPtr _voxelColorsDevMipMapPtr;
        cudaPitchedPtr _voxelNormalsDevMipMapPtr;
        //voxel cpu memory
        VoxelColorMipMaps _voxelColorMipMaps;
        VoxelNormalMipMaps _voxelNormalMipMaps;
        //octree gpu memory
        OctTreeDeviceBuffers _octTreeDeviceBuffers;
        //octree cpu memory
        OctTreeNodes _octTreeNodes;
        OctTreeConstColors _octTreeConstColors;
        size_t _octTreeNodesWriteIndex;
        std::stringstream _error;
    public:
        Voxelizer(const glm::uvec3& voxDim,
                  const glm::uvec3& brickDim,
                  const glm::uvec3& voxChunkDim=glm::uvec3(256u));

        ~Voxelizer();

        void addTriangleGroup(const std::vector<glm::vec3>* pVerts,
                              const std::vector<glm::vec3>* pVertNormals,
                              const std::vector<glm::vec2>* pUVs,
                              const glm::vec3& minXYZ,
                              const glm::vec3& maxXYZ,
                              bool isTerrain,
                              unsigned char* pImageData=nullptr,
                              int imageWidth=0,
                              int imageHeight=0,
                              cudaTextureAddressMode texAddressMode0=cudaAddressModeClamp,
                              cudaTextureAddressMode texAddressMode1=cudaAddressModeClamp)
        {
            glm::uvec3 minVox = static_cast<glm::uvec3>(glm::clamp(((minXYZ - this->_offsetP) / this->_deltaP),
                                           glm::vec3(0.0), glm::vec3(this->_voxDim + (this->_extraVoxChunk * 4u))));
            
            glm::uvec3 maxVox = static_cast<glm::uvec3>(glm::clamp(((maxXYZ - this->_offsetP) / this->_deltaP),
                                           glm::vec3(0.0), glm::vec3(this->_voxDim + (this->_extraVoxChunk * 4u))));

            cudaChannelFormatDesc imageDesc =  cudaCreateChannelDesc(8, 8, 8, 8, cudaChannelFormatKindUnsigned);
            _triangleGroups.push_back(TriangleGroup(pVerts, 
                                                    pVertNormals,
                                                    pUVs,
                                                    minVox,
                                                    maxVox,
                                                    isTerrain,
                                                    pImageData,
                                                    imageDesc,
                                                    imageWidth,
                                                    imageHeight,
                                                    texAddressMode0,
                                                    texAddressMode1));
            _totalVertCount += pVerts->size();
            _totalVertNormalsCount += pVertNormals != nullptr ? pVertNormals->size() : 0;
            _totalUVCount += (pImageData != nullptr && pUVs != nullptr) ? pUVs->size() : 0;
        }

        void setVoxelizationParams(const glm::vec3& p,
                                   const glm::vec3& deltaP);
        bool initCuda();
        bool resetCuda();
        bool allocateTriangleMemory();
        void deallocateTriangleMemory();
        bool allocateVoxelMemory();
        void deallocateVoxelMemory();

        bool computeEdgesFaceNormalsAndBounds();
        int generateVoxelsAndOctTree(const std::string& outputDir,
                                   bool outputBinary,
                                   bool outputCompressed);

        const std::string& getErrorMessage();
        const glm::uvec3& getExtraVoxChunk() { return _extraVoxChunk; }

    protected:
        bool initOctTreeDeviceBuffers(OctTreeDeviceBuffers& octTreeDevBuffers,
                                             size_t depth);
        bool initVoxelBrickWriters(const std::string& outputDir, 
                                   bool outputBinary,
                                   bool outputCompressed,
                                   VoxelBrickWriters& voxelBrickWriters);

        bool writeOctTree(const std::string& outputDir,
                          bool outputBinary,
                          bool outputCompressed,
                          VoxelBrickWriters& voxelBrickWriters);

        bool copyDeviceChunkToHostMipMapChunk(VoxelColors& voxelColors,
                                              VoxelNormals& voxelNormals,
                                              size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                              const cudaPitchedPtr& voxelColorsDevPtr,
                                              const cudaPitchedPtr& voxelNormalsDevPtr);

        bool allocateDeviceMipMaps(size_t xSize, size_t ySize, size_t zSize,
                             VoxelDeviceMipMaps& voxelDeviceMipMaps);

        bool allocateDeviceMipMaps(const glm::uvec3& voxChunkDim,
                                   cudaPitchedPtr& voxelColorsDevPtr,
                                   cudaPitchedPtr& voxelNormalsDevPtr,
                                   VoxelDeviceMipMaps& voxelDeviceMipMaps);

        bool allocateDeviceVoxelizationMemory(const glm::uvec3& voxChunkDim,
                                              cudaPitchedPtr& voxelColorsDevPtr,
                                              cudaPitchedPtr& voxelTriCountsDevPtr);

        bool allocateOctTreeDeviceBuffers(OctTreeDeviceBuffers& octTreeDevBuffers,
                                          size_t depth=0);

        void allocateHostMipMaps(VoxelColorMipMaps& voxelColorMipMaps,
                                 VoxelNormalMipMaps& voxelNormalMipMaps,
                                 size_t xSize, size_t ySize, size_t zSize,
                                 size_t depth=0);

        bool computeMipMaps(size_t xSize, size_t ySize, size_t zSize, //dimension of current chunk
                            size_t mmXSize, size_t mmYSize, size_t mmZSize,               
                            VoxelDeviceMipMaps& voxelDeviceMipMaps,
                            VoxelColorMipMaps& voxelColorMipMaps,
                            VoxelNormalMipMaps& voxelNormalMipMaps,
                            size_t depth=1);
        
        void computeOctTreeNodes(size_t xOffset, size_t yOffset, size_t zOffset,
                                 VoxelDeviceMipMaps& voxelDeviceMipMaps,
                                 VoxelColorMipMaps& voxelColorMipMaps,
                                 VoxelNormalMipMaps& voxelNormalMipMaps,
                                 VoxelBrickWriters& voxelBrickWriters,
                                 const glm::uvec3& voxChunkDim, //dimension of the chunk of mip map that we are processing
                                 const glm::uvec3& offsetToBrickBorder,
                                 OctTreeDeviceBuffers& octTreeDevBuffers,
                                 OctTreeNodes& octTreeNodes,
                                 OctTreeConstColors& octTreeConstColors,
                                 size_t writeIndex,
                                 size_t depth);

        void freeVoxelDeviceMipMaps();
        void freeTriangleDeviceMemory();
        void freeVoxelMipMapsAndOctTree();
        void freeTriangleHostMemory();
    };
};

#endif
