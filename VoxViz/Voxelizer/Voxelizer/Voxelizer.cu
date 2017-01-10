#define GLM_FORCE_CUDA
#include "Voxelizer.h"

#include <fstream>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <cmath>
#include <stdio.h>
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <list>

using namespace cuda;

//kernel declaration
__global__ void ComputeEdgesFaceNormalsAndBounds(const glm::vec3* pVerts,
                                                 size_t numVerts, 
                                                 glm::vec3 p,
                                                 glm::vec3 deltaP,
                                                 glm::uvec3 voxDim,
                                                 glm::vec3* pEdges, 
                                                 cuda::VoxNorm* pNormals, 
                                                 cuda::VoxelBBox* pBounds);

__global__ void ComputeVoxelization(const glm::vec3* pVerts,
                                    size_t triOffset, 
                                    const glm::vec3* pEdges, 
                                    const cuda::VoxNorm* pNormals, 
                                    const cuda::VoxelBBox* pBounds,
                                    glm::vec3 p,
                                    glm::vec3 deltaP,
                                    glm::uvec3 minVoxChunk,
                                    glm::uvec3 maxVoxChunk,
                                    glm::uint zOffset,
                                    cudaPitchedPtr pdevVoxelTriCounts,
                                    cudaPitchedPtr pdevVoxelTriIndices);

//__global__ void ComputeVoxelizationAverages(cudaPitchedPtr pvoxelTriCountsDevPtr,
//                                            cudaPitchedPtr voxelColorsDevPtr,
//                                            glm::uvec3 voxDim);

bool BindTextureToArray(cudaArray* pgImageArray,
                        cudaChannelFormatDesc imageDesc,
                        cudaTextureAddressMode addressMode0,
                        cudaTextureAddressMode addressMode1);

__global__ void ComputeColorsAndNormals(size_t triGrpOffset,
                                        const glm::vec3* pVerts,
                                        const glm::vec3* pVtxNormals,
                                        const glm::vec3* pFaceNormals,
                                        const glm::vec2* pUVs,
                                        bool isTerrain,
                                        glm::vec3 voxOrigin,
                                        glm::vec3 voxSizeMeters,
                                        cudaPitchedPtr voxelTriCountsDevPtr,
                                        cudaPitchedPtr voxelTriIndicesDevPtr,
                                        cudaPitchedPtr voxelColorsDevMipMapPtr,
                                        cudaPitchedPtr voxelNormalsDevMipMapPtr,
                                        glm::uvec3 voxWriteDim);

__global__ void ComputeColorsAsNormals(size_t triGrpOffset,
                                       const glm::vec3* pVerts,
                                       const glm::vec3* pVtxNormals,
                                       const glm::vec3* pFaceNormals,
                                       glm::vec3 voxOrigin,
                                       glm::vec3 voxSizeMeters,
                                       cudaPitchedPtr voxelTriCountsDevPtr,
                                       cudaPitchedPtr voxelTriIndicesDevPtr,
                                       cudaPitchedPtr voxelColorsDevMipMapPtr,
                                       cudaPitchedPtr voxelNormalsDevMipMapPtr,
                                       glm::uvec3 voxWriteDim);


__global__ void ComputeVoxelMipMap64(
                        glm::uint voxInputDimX, glm::uint voxInputDimY, glm::uint voxInputDimZ,//input size
                        cudaPitchedPtr inputVoxelColors,//input colors
                        cudaPitchedPtr inputVoxelNormals,//input normals
                        glm::uint voxDimX, glm::uint voxDimY, glm::uint voxDimZ,//output size
                        cudaPitchedPtr mipMapColors,//output colors
                        cudaPitchedPtr mipMapNormals);//output normals

__global__ void ComputeVoxelMipMap8(
                        glm::uint voxInputDimX, glm::uint voxInputDimY, glm::uint voxInputDimZ,//input size
                        cudaPitchedPtr inputVoxelColors,//input colors
                        cudaPitchedPtr inputVoxelNormals,//input normals
                        glm::uint voxDimX, glm::uint voxDimY, glm::uint voxDimZ,//output size
                        cudaPitchedPtr mipMapColors,//output colors
                        cudaPitchedPtr mipMapNormals);//output normals

__global__ void ComputeOctTreeNodeConstColor(cudaPitchedPtr pVoxelColors, 
                                             cuda::VoxColor* pOctTreeNodesConstColorPtr,
                                             glm::ivec3 voxOffset,
                                             glm::ivec3 offsetToBrickBorder,
                                             glm::ivec3 voxDim,
                                             glm::uvec3 octTreeChunkDim,
                                             glm::uvec3 octTreeDim,
                                             glm::ivec3 nodeOffset,
                                             glm::uvec3 brickDim);

__global__ void ComputeOctTreeNodeType(cudaPitchedPtr pVoxelColors, 
                                       glm::uint* pOctTreeNodesDevPtr,
                                       cuda::VoxColor* pOctTreeNodesConstColorPtr,
                                       glm::ivec3 voxOffset,
                                       glm::ivec3 offsetToBrickBorder,
                                       //glm::ivec3 brickSampleOffset,
                                       glm::ivec3 voxDim,
                                       glm::ivec3 fullVoxDim,
                                       glm::uvec3 octTreeDim,
                                       glm::uvec3 brickDim,
                                       int xOffset);

static size_t s_totalAllocatedDeviceMemory = 0u;
static cudaError_t s_cudaStatus = cudaSuccess;
#ifdef __DEBUG__
static bool s_debug_svg = false;
static bool s_debug_mipmaps = false;
static bool s_debug_output = false;
static bool s_debug_voxels = false;
static bool s_debug_scalars = false;
#endif

Voxelizer::Voxelizer(const glm::uvec3& voxDim,
                     const glm::uvec3& brickDim,
                     const glm::uvec3& voxChunkDim) :
    _pgVerts(0),
    _pgEdges(0),
    _pgFaceNormals(0),
    _pgVtxNormals(0),
    _pgBounds(0),
    _pgUVs(0),
    _totalVertCount(0),
    _totalVertNormalsCount(0),
    _totalUVCount(0),
    _voxDim(voxDim),
    _voxChunkDim(voxChunkDim),
    _brickDim(brickDim),
    _octTreeNodesWriteIndex(0u)
{
    //compute ratio full res voxels to lowest res voxels
    _extraVoxChunk = (_voxDim / _brickDim);
    _extraVoxChunk >>= 1u;
    //move the origin to new edge of voxels
    _numMipMapLevels = 1;
    glm::uvec3 mipMapDim = _brickDim;
    while(mipMapDim != voxDim)
    {
        mipMapDim <<= 1;
        ++_numMipMapLevels;
    }
 }

void Voxelizer::setVoxelizationParams(const glm::vec3& p,
                                      const glm::vec3& deltaP)
{
    _p = p;
    _deltaP = deltaP;
    _offsetP = _p - (static_cast<glm::vec3>(_extraVoxChunk) * _deltaP);
}   
 
bool Voxelizer::allocateVoxelMemory()
{
    glm::uvec3 voxChunkDim = _voxChunkDim;
    VoxelColorMipMaps& voxelColorMipMaps = _voxelColorMipMaps;
    voxelColorMipMaps.resize(_numMipMapLevels);
    VoxelNormalMipMaps& voxelNormalMipMaps = _voxelNormalMipMaps;
    voxelNormalMipMaps.resize(_numMipMapLevels);
    allocateHostMipMaps(voxelColorMipMaps, voxelNormalMipMaps,
                        voxChunkDim.x, //allocate nearest multiple of four
                        voxChunkDim.y, 
                        voxChunkDim.z);//allocate with 1 voxel border

    //allocate memory for voxel mip maps
    if(!allocateDeviceVoxelizationMemory(voxChunkDim,
                                         _voxelTriIndicesDevPtr,
                                         _voxelTriCountsDevPtr))
    {
        return false;
    }

    if(!allocateDeviceMipMaps(voxChunkDim,
                              _voxelColorsDevMipMapPtr,
                              _voxelNormalsDevMipMapPtr,
                              _voxelDeviceMipMaps))
    {
        return false;
    }

    _octTreeDeviceBuffers.resize(voxelColorMipMaps.size());
    if(!allocateOctTreeDeviceBuffers(_octTreeDeviceBuffers,
                                     voxelColorMipMaps.size()-1u))
    {
        return false;
    }

    size_t totalNodeCount = 0;
    _octTreeNodesWriteIndex = 0;
    for(size_t i = 0; i < voxelColorMipMaps.size(); ++i)
    {
        if(i + 1 == voxelColorMipMaps.size())
            _octTreeNodesWriteIndex = totalNodeCount;//offset to start writing leaf nodes

        totalNodeCount += static_cast<size_t>(glm::pow(8.0f, static_cast<float>(i)));
    }

    _octTreeNodes.resize(totalNodeCount, 0u);
    cuda::VoxColor zeros;
    zeros.x = zeros.y = zeros.z = zeros.w = 0;
    _octTreeConstColors.resize(totalNodeCount, zeros);

    return true;
}

void Voxelizer::deallocateVoxelMemory()
{
    cudaFree(_voxelTriIndicesDevPtr.ptr);
    cudaFree(_voxelTriCountsDevPtr.ptr);

    freeVoxelDeviceMipMaps();

    //unsigned int* pTest = new unsigned int[ octTreeNodesDim.x * octTreeNodesDim.y * octTreeNodesDim.z];
    //cudaMemcpy(pTest, pOctTreeNodesDevPtr, octTreeSize, cudaMemcpyDeviceToHost);
    for(size_t i = 0; i < _octTreeDeviceBuffers.size(); ++i)
    {
        cudaFree(_octTreeDeviceBuffers[i].pTypes);
        cudaFree(_octTreeDeviceBuffers[i].pConstColors);
    }
    _octTreeDeviceBuffers.clear();

    s_totalAllocatedDeviceMemory = 0u;

    freeVoxelMipMapsAndOctTree();
}

void Voxelizer::deallocateTriangleMemory()
{
    freeTriangleHostMemory();

    freeTriangleDeviceMemory();
}

Voxelizer::~Voxelizer()
{
    deallocateTriangleMemory();

    deallocateVoxelMemory();
}

void Voxelizer::freeTriangleHostMemory()
{
    _triangleGroups.clear();
    TriangleGroups empty;
    empty.swap(_triangleGroups);
    _totalVertCount = 0;
    _totalVertNormalsCount = 0;
    _totalUVCount = 0;
}

void Voxelizer::freeTriangleDeviceMemory()
{
    if(_pgVerts)
        cudaFree(_pgVerts);
    _pgVerts = nullptr;

    if(_pgEdges)
        cudaFree(_pgEdges);
    _pgEdges = nullptr;

    if(_pgFaceNormals)
        cudaFree(_pgFaceNormals);
    _pgFaceNormals = nullptr;

    if(_pgVtxNormals)
        cudaFree(_pgVtxNormals);
    _pgVtxNormals = nullptr;

    if(_pgBounds)
        cudaFree(_pgBounds);
    _pgBounds = nullptr;

    if(_pgUVs)
        cudaFree(_pgUVs);
    _pgUVs = nullptr;

    for(ImageMap::iterator itr = _imageMap.begin();
        itr != _imageMap.end();
        ++itr)
    {
        if(itr->second)
            cudaFreeArray(itr->second);
    }
    _imageMap.clear();
}

bool Voxelizer::initCuda()
{
    // Choose which GPU to run on, change this on a multi-GPU system.
    s_cudaStatus = cudaSetDevice(0);
    if(s_cudaStatus != cudaSuccess) 
    {
        _error << "cudaSetDevice failed!  Do you have a CUDA-capable GPU installed?" << std::endl;
        return false;
    }

    cudaDeviceGetAttribute(&_maxThreadsPerBlock, cudaDevAttrMaxThreadsPerBlock, 0);
    //cudaDeviceGetAttribute(&_maxBlockDim.x, cudaDevAttrMaxBlockDimX, 0);
    //cudaDeviceGetAttribute(&_maxBlockDim.y, cudaDevAttrMaxBlockDimY, 0);
    //cudaDeviceGetAttribute(&_maxBlockDim.z, cudaDevAttrMaxBlockDimZ, 0);
    
    int maxBlockDim = static_cast<int>(glm::pow(static_cast<float>(_maxThreadsPerBlock), 1.0f / 3.0f));
    //need largest multiple of four that is less than cube root
    maxBlockDim -= (maxBlockDim % 4);

    _maxBlockDim = glm::ivec3(maxBlockDim);

    //for some reason I can't run a 1D kernel with the max amount of threads of 1024
    //the largest I've been able to get is 960, so cutting this in half to make it work
    _maxThreadsPerBlock >>= 1;
    return true;
}

bool Voxelizer::resetCuda()
{
    deallocateTriangleMemory();

    deallocateVoxelMemory();

    s_cudaStatus = cudaDeviceReset();
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaDeviceReset failed, error code=" << s_cudaStatus << std::endl;
        return false;
    }

    if(!allocateVoxelMemory())
    {
        _error << "allocateVoxelMemory failed." << std::endl;
        return false;
    }

    return true;
}

bool Voxelizer::allocateTriangleMemory()
{
    size_t vertSize = sizeof(glm::vec3) * _totalVertCount;
    s_cudaStatus = cudaMalloc(&_pgVerts, vertSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMalloc failed to allocate vertex buffer " << vertSize << " bytes." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += vertSize;

    size_t numTris = _totalVertCount / 3;

    size_t normalsSize = (numTris * sizeof(glm::vec3));
    s_cudaStatus = cudaMalloc(&_pgFaceNormals, normalsSize);
    if(s_cudaStatus != cudaSuccess) 
    {
        _error << "cudaMalloc failed to allocate normals buffer " << normalsSize << " bytes." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += normalsSize;

    if(_totalVertNormalsCount > 0u)
    {
        //size_t numTrisWithVtxNormals = _totalVertNormalsCount / 3;
        size_t vtxNormalsSize = sizeof(glm::vec3) * _totalVertNormalsCount;
        s_cudaStatus = cudaMalloc(&_pgVtxNormals, vtxNormalsSize);
        if(s_cudaStatus != cudaSuccess) 
        {
            _error << "cudaMalloc failed to allocate vertex normals buffer " << vtxNormalsSize << " bytes." << std::endl;
            return false;
        }
        s_totalAllocatedDeviceMemory += vtxNormalsSize;
    }

    size_t uvSize = 0u;
    if(_totalUVCount > 0u)
    {
        uvSize = sizeof(glm::vec2) * _totalUVCount;
        s_cudaStatus = cudaMalloc(&_pgUVs, uvSize);
        if(s_cudaStatus != cudaSuccess)
        {
            _error << "cudaMalloc failed to allocate uv buffer " << uvSize << " bytes." << std::endl;
            return false;
        }
        s_totalAllocatedDeviceMemory += uvSize;
    }

    size_t vertOffset = 0u;
    size_t normalsOffset = 0u;
    size_t uvOffset = 0u;
    for(TriangleGroups::iterator itr = _triangleGroups.begin();
        itr != _triangleGroups.end();
        ++itr)
    {
        size_t grpVertSize = itr->pVerts->size() * sizeof(glm::vec3);
        s_cudaStatus = cudaMemcpy(&_pgVerts[vertOffset], 
                                  &itr->pVerts->front(),
                                  grpVertSize, 
                                  cudaMemcpyHostToDevice);
        if(s_cudaStatus != cudaSuccess)
        {
            _error << "cudaMemcpy failed for vertex buffer error=" << s_cudaStatus << "." << std::endl;
            return false;
        }
        vertOffset += itr->pVerts->size();

        if(itr->pVertNormals != nullptr)
        {
            size_t grpVertNormalsSize = itr->pVertNormals->size() * sizeof(glm::vec3);
            s_cudaStatus = cudaMemcpy(&_pgVtxNormals[normalsOffset],
                                      &itr->pVertNormals->front(),
                                      grpVertNormalsSize,
                                      cudaMemcpyHostToDevice);
            normalsOffset += itr->pVertNormals->size();
        }

        if(itr->pImageData != nullptr && itr->pUVs != nullptr)
        {
            size_t grpUVSize = itr->pUVs->size() * sizeof(glm::vec2);
            s_cudaStatus = cudaMemcpy(&_pgUVs[uvOffset], 
                                      &itr->pUVs->front(),
                                      grpUVSize, 
                                      cudaMemcpyHostToDevice);
            if(s_cudaStatus != cudaSuccess)
            {
                _error << "cudaMemcpy failed for uv buffer error=" << s_cudaStatus << "." << std::endl;
                return false;
            }
            uvOffset += itr->pUVs->size();
        }
    }
    
    s_cudaStatus = cudaMalloc(&_pgEdges, vertSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMalloc failed to allocate edges buffer " << vertSize << " bytes." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += vertSize;

    size_t boundsSize = numTris * sizeof(VoxelBBox);
    s_cudaStatus = cudaMalloc(&_pgBounds, boundsSize);
    if(s_cudaStatus != cudaSuccess) 
    {
        _error << "cudaMalloc failed to allocate bounds buffer " << boundsSize << " bytes." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += boundsSize;

    return true;
}

bool Voxelizer::computeEdgesFaceNormalsAndBounds()
{
    size_t numTris = _totalVertCount / 3;
    dim3 threadsPerBlock;
    dim3 numBlocks;
    if(static_cast<size_t>(_maxThreadsPerBlock) > numTris)
        threadsPerBlock.x = static_cast<glm::uint>(numTris);
    else
    {
        threadsPerBlock.x = _maxThreadsPerBlock;
        numBlocks.x = static_cast<size_t>(glm::ceil((static_cast<float>(numTris) / static_cast<float>(_maxThreadsPerBlock))));
    }

    glm::uvec3 voxDimPlusExtra = _voxDim + (_extraVoxChunk << glm::uvec3(2u));

    ComputeEdgesFaceNormalsAndBounds<<<numBlocks, threadsPerBlock>>>(_pgVerts, _totalVertCount, 
                                                          _offsetP, _deltaP, 
                                                          voxDimPlusExtra, 
                                                          _pgEdges,
                                                          _pgFaceNormals,
                                                          _pgBounds);

    s_cudaStatus = cudaDeviceSynchronize();
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "ComputeEdgesFaceNormalsAndBounds() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }

    return true;
}

bool Voxelizer::computeMipMaps(size_t xSize, size_t ySize, size_t zSize, //dimension of current chunk
                               size_t mmXSize, size_t mmYSize, size_t mmZSize,
                               VoxelDeviceMipMaps& voxelDeviceMipMaps,
                               VoxelColorMipMaps& voxelColorMipMaps,
                               VoxelNormalMipMaps& voxelNormalMipMaps,
                               size_t depth/*=1*/)
{
    //figure out the kernel dimensions, need one thread per voxel of mip map
    dim3 blocksPerGrid(1u, 1u, 1u);
    dim3 threadsPerBlock(static_cast<glm::uint>(mmXSize), 
                         static_cast<glm::uint>(mmYSize), 
                         static_cast<glm::uint>(mmZSize));
    if(mmXSize > static_cast<glm::uint>(_maxBlockDim.x) || 
       mmYSize > static_cast<glm::uint>(_maxBlockDim.y) || 
       mmZSize > static_cast<glm::uint>(_maxBlockDim.z))
    {
        threadsPerBlock.x = glm::min(static_cast<glm::uint>(mmXSize), static_cast<glm::uint>(_maxBlockDim.x));
        threadsPerBlock.y = glm::min(static_cast<glm::uint>(mmYSize), static_cast<glm::uint>(_maxBlockDim.y));
        threadsPerBlock.z = glm::min(static_cast<glm::uint>(mmZSize), static_cast<glm::uint>(_maxBlockDim.z));

        blocksPerGrid.x = 
            static_cast<size_t>(glm::ceil((static_cast<float>(mmXSize) / static_cast<float>(threadsPerBlock.x))));
        blocksPerGrid.y = 
            static_cast<size_t>(glm::ceil((static_cast<float>(mmYSize) / static_cast<float>(threadsPerBlock.y))));
        blocksPerGrid.z = 
            static_cast<size_t>(glm::ceil((static_cast<float>(mmZSize) / static_cast<float>(threadsPerBlock.z))));
    }

    size_t inputDepth;
    if(depth == 1)
        inputDepth = depth - 1;
    else
        inputDepth = depth - 2;
    //input voxel mip maps
    const VoxelDataPtrs& inputVoxelsDevPtrs = voxelDeviceMipMaps[inputDepth];

    //output voxel mip maps
    VoxelDataPtrs& mipMapPtrs = voxelDeviceMipMaps[depth];
    cudaPitchedPtr& outputColorsDevPtr = mipMapPtrs.first;
    cudaPitchedPtr& outputNormalsDevPtr = mipMapPtrs.second;

    size_t xScale = static_cast<size_t>(glm::ceil(static_cast<float>(xSize) / static_cast<float>(mmXSize)));
    size_t yScale = static_cast<size_t>(glm::ceil(static_cast<float>(ySize) / static_cast<float>(mmYSize)));
    size_t zScale = static_cast<size_t>(glm::ceil(static_cast<float>(zSize) / static_cast<float>(mmZSize)));
    //if(xScale == 2)
    {
        ComputeVoxelMipMap8<<<blocksPerGrid, threadsPerBlock>>>(
                                                               static_cast<glm::uint>(xSize), 
                                                               static_cast<glm::uint>(ySize), 
                                                               static_cast<glm::uint>(zSize),
                                                               inputVoxelsDevPtrs.first,//input colors
                                                               inputVoxelsDevPtrs.second,//input normals
                                                               static_cast<glm::uint>(mmXSize), 
                                                               static_cast<glm::uint>(mmYSize), 
                                                               static_cast<glm::uint>(mmZSize),//output size
                                                               outputColorsDevPtr,//output colors
                                                               outputNormalsDevPtr);//output normals
    }
    /*else
    {
            ComputeVoxelMipMap64<<<blocksPerGrid, threadsPerBlock>>>(
                                                               static_cast<glm::uint>(xSize), 
                                                               static_cast<glm::uint>(ySize), 
                                                               static_cast<glm::uint>(zSize),
                                                               inputVoxelsDevPtrs.first,//input colors
                                                               inputVoxelsDevPtrs.second,//input normals
                                                               static_cast<glm::uint>(mmXSize), 
                                                               static_cast<glm::uint>(mmYSize), 
                                                               static_cast<glm::uint>(mmZSize),//output size
                                                               outputColorsDevPtr,//output colors
                                                               outputNormalsDevPtr);//output normals
    }*/
    s_cudaStatus = cudaDeviceSynchronize();
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "computeMipMaps() error: " << s_cudaStatus << " returned from cudaDeviceSynchronize." << std::endl;
        return false;
    }

    if(depth != _numMipMapLevels-1)
    {
        if(!computeMipMaps(depth == 1 ? xSize : xSize>>1, 
                           depth == 1 ? ySize : ySize>>1,
                           depth == 1 ? zSize : zSize>>1,
                           mmXSize>>1, mmYSize>>1, mmZSize>>1,
                           voxelDeviceMipMaps,
                           voxelColorMipMaps,
                           voxelNormalMipMaps,
                           depth+1))
        {
            return false;
        }
    }

    VoxelColorMipMap& voxelColors = voxelColorMipMaps.at(depth);
    VoxelNormalMipMap& voxelNormals = voxelNormalMipMaps.at(depth);
    //copy this chunk of the mip map to the host
    copyDeviceChunkToHostMipMapChunk(voxelColors.colors,
                                     voxelNormals.normals,
                                     mmXSize, mmYSize, mmZSize,
                                     outputColorsDevPtr,
                                     outputNormalsDevPtr);
    return true;
}

bool Voxelizer::allocateDeviceMipMaps(size_t xSize, size_t ySize, size_t zSize, //dimension of current chunk
                                VoxelDeviceMipMaps& voxelDeviceMipMaps)
{
    //compute size of mip map chunk
    size_t mmXSize = static_cast<size_t>(glm::ceil(xSize * 0.5f));

    size_t mmYSize = static_cast<size_t>(glm::ceil(ySize * 0.5f));
    
    size_t mmZSize = static_cast<size_t>(glm::ceil(zSize * 0.5f));

    cudaExtent ext3D = make_cudaExtent(mmXSize * sizeof(cuda::VoxColor),
                                       mmYSize,
                                       mmZSize);
    cudaPitchedPtr mipMapColors;
    s_cudaStatus = cudaMalloc3D(&mipMapColors, 
                                ext3D);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << " returned from cudaMalloc3D." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (ext3D.width * ext3D.height * ext3D.depth);

    ext3D = make_cudaExtent(mmXSize * sizeof(cuda::VoxNorm),
                            mmYSize,
                            mmZSize);

    cudaPitchedPtr mipMapNormals;
    s_cudaStatus = cudaMalloc3D(&mipMapNormals,
                                ext3D);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << " returned from cudaMalloc3D." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (ext3D.width * ext3D.height * ext3D.depth);

    voxelDeviceMipMaps.push_back(VoxelDataPtrs(mipMapColors, mipMapNormals));

    if(voxelDeviceMipMaps.size() != _numMipMapLevels)
    {
        return allocateDeviceMipMaps(mmXSize, mmYSize, mmZSize,
                                     voxelDeviceMipMaps);
    }

    return true;
}

void Voxelizer::allocateHostMipMaps(VoxelColorMipMaps& voxelColorMipMaps,
                                    VoxelNormalMipMaps& voxelNormalMipMaps,
                                    size_t xSize, size_t ySize, size_t zSize,
                                    size_t depth/*=0*/)
{    
    VoxelColorMipMap& voxelColors = voxelColorMipMaps.at(depth);
    voxelColors.dim.x = xSize;
    voxelColors.dim.y = ySize;
    voxelColors.dim.z = zSize;
    voxelColors.colors.resize(xSize * ySize * zSize);

    VoxelNormalMipMap& voxelNormals = voxelNormalMipMaps.at(depth);
    voxelNormals.dim.x = xSize;
    voxelNormals.dim.y = ySize;
    voxelNormals.dim.z = zSize;
    voxelNormals.normals.resize(xSize * ySize * zSize);

    if((depth + 1) != _numMipMapLevels)
    {
        size_t mmXSize = static_cast<size_t>(glm::ceil(xSize * 0.5f));
        size_t mmYSize = static_cast<size_t>(glm::ceil(ySize * 0.5f));
        size_t mmZSize = static_cast<size_t>(glm::ceil(zSize * 0.5f));
        allocateHostMipMaps(voxelColorMipMaps, voxelNormalMipMaps,
                            mmXSize, mmYSize, mmZSize,
                            depth+1);
    }
}

//static void LoadDeviceMemory(size_t xOffset, size_t yOffset, size_t zOffset,
//                             size_t xSize, size_t ySize, size_t zSize,
//                             cuda::VoxColor* pVoxelsDevPtr,
//                             const VoxelColors& voxelColors,
//                             const glm::uvec3& voxDim)
//{
//    for(size_t z = zOffset; z < (zOffset + zSize); ++z)
//    {
//        for(size_t y = yOffset; y < (yOffset + ySize); ++y)
//        {
//            //TODO use this
//            /*cudaMemcpyAsync(pVoxelsDevPtr, 
//                            &voxelColors[(z * voxDim.x * voxDim.y) + (y * voxDim.x) + xOffset], 
//                            sizeof(cuda::VoxColor) * xSize,
//                            cudaMemcpyHostToDevice);*/
//            size_t startIndex = (z * voxDim.x * voxDim.y) + (y * voxDim.x) + xOffset;
//
//            cudaMemcpy(pVoxelsDevPtr, 
//                       &voxelColors[startIndex], 
//                       sizeof(cuda::VoxColor) * xSize,
//                       cudaMemcpyHostToDevice);
//
//            pVoxelsDevPtr += xSize;
//        }
//    }
//
//    cudaDeviceSynchronize();
//}

static bool CollapseConstantBranches(cuda::Voxelizer::OctTreeNodes& octTreeNodes,
                                     size_t curX=0,
                                     size_t curY=0,
                                     size_t curZ=0,
                                     size_t curXSize=1,
                                     size_t curYSize=1,
                                     size_t curZSize=1,
                                     size_t curStart=0,
                                     size_t childX=0,
                                     size_t childY=0,
                                     size_t childZ=0,
                                     size_t childXSize=2,
                                     size_t childYSize=2,
                                     size_t childZSize=2,
                                     size_t nextStartIndex=1)
{
    size_t childStartIndex = nextStartIndex + 
                             ((childZ * childYSize * childXSize) + (childY * childXSize) + childX);
    size_t curIndex = curStart + 
                      ((curZ * curYSize * curXSize) + (curY * curXSize) + curX);
    if(childStartIndex >= octTreeNodes.size())//if we are at a leaf node
       return octTreeNodes[curIndex] == 0;

    size_t gChildXSize = childXSize << 1;
    size_t gChildYSize = childYSize << 1;
    size_t gChildZSize = childZSize << 1;
    size_t gChildStartIndex = nextStartIndex + (childXSize * childYSize * childZSize);

    bool allChildrenAreConst = true;
    size_t endX = childX + 2;
    size_t endY = childY + 2;
    size_t endZ = childZ + 2;
    for(size_t zChild = childZ ; zChild < endZ; ++zChild)
    {
        size_t gChildZ = zChild << 1;
        for(size_t yChild = childY; yChild < endY; ++yChild)
        {
            size_t gChildY = yChild << 1;
            for(size_t xChild = childX; xChild < endX; ++xChild)
            {
                size_t gChildX = xChild << 1;
                if(!CollapseConstantBranches(octTreeNodes,
                                             xChild,
                                             yChild,
                                             zChild,
                                             childXSize,
                                             childYSize,
                                             childZSize,
                                             nextStartIndex,
                                             gChildX,
                                             gChildY,
                                             gChildZ,
                                             gChildXSize,
                                             gChildYSize,
                                             gChildZSize,
                                             gChildStartIndex))
                {
                    allChildrenAreConst = false;
                }
            }
        }
    }

    //if i am const (0) and all my children are too
    //then ok to collapse
    if(octTreeNodes[curIndex] == 0 &&
       allChildrenAreConst)
    {
        octTreeNodes[curIndex] = 2;
        return true;
    }

    return false;
}

static inline bool VEC4_EQUAL(const glm::vec4& v1, const glm::vec4& v2);
static inline bool VEC4_EQUAL(const uchar4& v1, const uchar4& v2);

static bool ValidateNode(size_t xOffset, size_t yOffset, size_t zOffset,
                    size_t xSize, size_t ySize, size_t zSize,
                    const glm::uvec3& brickDim,
                    const VoxelColors& voxelColors,
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
                    if(type == 1u)
                        return true;
                    else
                        return false;
                }
            }
        }
    }

    if(type == 0u || type == 2u)
        return true;
    else
        return false;
}

bool Voxelizer::allocateOctTreeDeviceBuffers(OctTreeDeviceBuffers& octTreeDevBuffers,
                                             size_t depth)
{
    size_t numNodes = static_cast<size_t>(glm::pow(8.0f, static_cast<float>(depth)));
    
    size_t octTreeSize = sizeof(glm::uint) * numNodes;

    glm::uint* pOctTreeNodesDevPtr = 0u;
    s_cudaStatus = cudaMalloc(&pOctTreeNodesDevPtr, 
                              octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMalloc failed to allocate buffer " << octTreeSize << " bytes." << std::endl;
        return false;
    }

    s_totalAllocatedDeviceMemory += octTreeSize;

    //initialize to zero, which is code for const-node, use 1 for non-const-node
    s_cudaStatus = cudaMemset(pOctTreeNodesDevPtr, 0, octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMemset failed "
               << s_cudaStatus
               << std::endl;
        return false;
    }

    octTreeDevBuffers[depth].pTypes = pOctTreeNodesDevPtr;

    octTreeSize = sizeof(cuda::VoxColor) * numNodes;

    cuda::VoxColor* pOctTreeNodesColorsDevPtr = 0u;
    s_cudaStatus = cudaMalloc(&pOctTreeNodesColorsDevPtr, 
                              octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMalloc failed to allocate buffer " << octTreeSize << " bytes." << std::endl;
        return false;
    }

    s_totalAllocatedDeviceMemory += octTreeSize;

    s_cudaStatus = cudaMemset(pOctTreeNodesColorsDevPtr, 0, octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMemset failed "
               << s_cudaStatus
               << std::endl;
        return false;
    }

    octTreeDevBuffers[depth].pConstColors = pOctTreeNodesColorsDevPtr;

    if(depth == 0)
        return true;

    return allocateOctTreeDeviceBuffers(octTreeDevBuffers, depth - 1);
}

bool Voxelizer::initOctTreeDeviceBuffers(OctTreeDeviceBuffers& octTreeDevBuffers,
                                         size_t depth)
{
    size_t numNodes = static_cast<size_t>(glm::pow(8.0f, static_cast<float>(depth)));
    
    size_t octTreeSize = sizeof(glm::uint) * numNodes;

    glm::uint* pOctTreeNodesDevPtr = octTreeDevBuffers[depth].pTypes;
    //initialize to zero, which is code for const-node, use 1 for non-const-node
    s_cudaStatus = cudaMemset(pOctTreeNodesDevPtr, 0, octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMemset failed "
               << s_cudaStatus
               << std::endl;
        return false;
    }

    octTreeSize = sizeof(cuda::VoxColor) * numNodes;

    cuda::VoxColor* pOctTreeNodesColorsDevPtr = octTreeDevBuffers[depth].pConstColors;
    
    s_cudaStatus = cudaMemset(pOctTreeNodesColorsDevPtr, 0, octTreeSize);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "cudaMemset failed "
               << s_cudaStatus
               << std::endl;
        return false;
    }

    if(depth == 0)
        return true;

    return initOctTreeDeviceBuffers(octTreeDevBuffers, depth - 1);
}

void Voxelizer::computeOctTreeNodes(size_t xOffset, size_t yOffset, size_t zOffset,
                                    VoxelDeviceMipMaps& voxelDeviceMipMaps,
                                    VoxelColorMipMaps& voxelColorMipMaps,
                                    VoxelNormalMipMaps& voxelNormalMipMaps,
                                    VoxelBrickWriters& voxelBrickWriters,
                                    const glm::uvec3& mipMapChunkDim, //dimension of the chunk of mip map that we are processing
                                    const glm::uvec3& offsetToBrickBorder,
                                    OctTreeDeviceBuffers& octTreeDevBuffers,
                                    OctTreeNodes& octTreeNodes,
                                    OctTreeConstColors& octTreeConstColors,
                                    size_t writeIndex,
                                    size_t depth)
{
    size_t numNodes = static_cast<size_t>(glm::pow(8.0f, static_cast<float>(depth)));
    size_t cubeRoot = static_cast<size_t>(glm::pow(static_cast<float>(numNodes), 1.0f / 3.0f));
    glm::uvec3 octTreeNodesDim(cubeRoot, cubeRoot, cubeRoot);

    const cudaPitchedPtr& voxelColors = voxelDeviceMipMaps.at(voxelDeviceMipMaps.size() - depth - 1u).first;

    glm::uint* pOctTreeNodesDevPtr = octTreeDevBuffers[depth].pTypes;
    cuda::VoxColor* pOctTreeNodesConstColorPtr = octTreeDevBuffers[depth].pConstColors;

    glm::ivec3 voxOffset(xOffset, 
                         yOffset, 
                         zOffset);
    //where to sample from the brick to do the matching against
    //glm::ivec3 brickSampleOffset(xOffset == 0u ? 0 : static_cast<int>(_brickDim.x - 1u),
    //                             yOffset == 0u ? 0 : static_cast<int>(_brickDim.y - 1u),
    //                             zOffset == 0u ? 0 : static_cast<int>(_brickDim.z - 1u));
    //this is the xyz of the starting offset without the extra border
    glm::ivec3 startVoxXYZ(xOffset - offsetToBrickBorder.x,
                           yOffset - offsetToBrickBorder.y,
                           zOffset - offsetToBrickBorder.z);

    size_t nodeXStart = 0u;
    if(startVoxXYZ.x >= 0)
        nodeXStart = static_cast<size_t>(static_cast<float>(glm::max(startVoxXYZ.x - 1, 0)) / static_cast<float>(_brickDim.x));
    size_t nodeYStart = 0u;
    if(startVoxXYZ.y >= 0)
        nodeYStart = static_cast<size_t>(static_cast<float>(glm::max(startVoxXYZ.y - 1, 0)) / static_cast<float>(_brickDim.y));
    size_t nodeZStart = 0u;
    if(startVoxXYZ.z >= 0)
        nodeZStart = static_cast<size_t>(static_cast<float>(glm::max(startVoxXYZ.z - 1, 0)) / static_cast<float>(_brickDim.z));

    //this is the xyz of the starting offset without the extra border
    glm::ivec3 endVoxXYZ((xOffset + mipMapChunkDim.x - 1) - offsetToBrickBorder.x,
                         (yOffset + mipMapChunkDim.y - 1) - offsetToBrickBorder.y,
                         (zOffset + mipMapChunkDim.z - 1) - offsetToBrickBorder.z);

    size_t nodeXEnd = static_cast<size_t>(static_cast<float>(endVoxXYZ.x + 1) / static_cast<float>(_brickDim.x));
    if(nodeXEnd >= octTreeNodesDim.x)
        nodeXEnd = octTreeNodesDim.x - 1u;
    size_t nodeYEnd = static_cast<size_t>(static_cast<float>(endVoxXYZ.y + 1) / static_cast<float>(_brickDim.y));
    if(nodeYEnd >= octTreeNodesDim.y)
        nodeYEnd = octTreeNodesDim.y - 1u;
    size_t nodeZEnd = static_cast<size_t>(static_cast<float>(endVoxXYZ.z + 1) / static_cast<float>(_brickDim.z));
    if(nodeZEnd >= octTreeNodesDim.z)
        nodeZEnd = octTreeNodesDim.z - 1u;

    glm::uvec3 nodesChunkDim(nodeXEnd - nodeXStart + 1u,
                       nodeYEnd - nodeYStart + 1u,
                       nodeZEnd - nodeZStart + 1u);
    //one block per node
    //need one thread per voxel
    //if my voxel grid is larger than the amount of device memory then we'll have to break it
    //up into multiple executions
    dim3 numBlocksPerGrid(1u, 1u, 1u);
    dim3 numThreadsPerBlock(nodesChunkDim.x, nodesChunkDim.y, nodesChunkDim.z);
    //dim3 numThreadsPerBlock(128u, 128u, 128u);
    if(numThreadsPerBlock.x > static_cast<glm::uint>(_maxBlockDim.x) || 
       numThreadsPerBlock.y > static_cast<glm::uint>(_maxBlockDim.y) || 
       numThreadsPerBlock.z > static_cast<glm::uint>(_maxBlockDim.z))
    {
        numThreadsPerBlock.x = glm::min(numThreadsPerBlock.x, static_cast<glm::uint>(_maxBlockDim.x));
        numThreadsPerBlock.y = glm::min(numThreadsPerBlock.y, static_cast<glm::uint>(_maxBlockDim.y));
        numThreadsPerBlock.z = glm::min(numThreadsPerBlock.z, static_cast<glm::uint>(_maxBlockDim.z));

        numBlocksPerGrid.x = 
            static_cast<size_t>(glm::ceil((static_cast<float>(nodesChunkDim.x) / static_cast<float>(numThreadsPerBlock.x))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.x))));
        numBlocksPerGrid.y = 
            static_cast<size_t>(glm::ceil((static_cast<float>(nodesChunkDim.y) / static_cast<float>(numThreadsPerBlock.y))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.y))));
        numBlocksPerGrid.z = 
            static_cast<size_t>(glm::ceil((static_cast<float>(nodesChunkDim.z) / static_cast<float>(numThreadsPerBlock.z))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.z))));
    }

    ComputeOctTreeNodeConstColor<<<numBlocksPerGrid, numThreadsPerBlock>>>(voxelColors, 
                                                                           pOctTreeNodesConstColorPtr,
                                                                           voxOffset,
                                                                           glm::ivec3(offsetToBrickBorder.x,
                                                                                      offsetToBrickBorder.y,
                                                                                      offsetToBrickBorder.z),
                                                                           glm::ivec3(mipMapChunkDim.x,
                                                                                      mipMapChunkDim.y,
                                                                                      mipMapChunkDim.z),
                                                                           nodesChunkDim,
                                                                           octTreeNodesDim,
                                                                           glm::ivec3(nodeXStart, 
                                                                                      nodeYStart,
                                                                                      nodeZStart),
                                                                           _brickDim);
    s_cudaStatus = cudaDeviceSynchronize();
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "computeOctTreeNodes() error: " << s_cudaStatus << "." << std::endl;
        return;
    }


    glm::ivec3 fullVoxDim(octTreeNodesDim.x * _brickDim.x,
                        octTreeNodesDim.y * _brickDim.y,
                        octTreeNodesDim.z * _brickDim.z);
    //one block per node
    //need one thread per voxel
    //if my voxel grid is larger than the amount of device memory then we'll have to break it
    //up into multiple executions
    numBlocksPerGrid = dim3(1u, 1u, 1u);
    glm::uvec3 tpb(mipMapChunkDim.x, mipMapChunkDim.y, mipMapChunkDim.z);
    glm::uvec3 fullVoxOffset(tpb.x + voxOffset.x - offsetToBrickBorder.x,
                             tpb.y + voxOffset.y - offsetToBrickBorder.y,
                             tpb.z + voxOffset.z - offsetToBrickBorder.z);
    if(fullVoxOffset.x > fullVoxDim.x)
        tpb.x = fullVoxDim.x - (voxOffset.x - offsetToBrickBorder.x) + 2u;
    if(fullVoxOffset.y > fullVoxDim.y)
        tpb.y = fullVoxDim.y - (voxOffset.y - offsetToBrickBorder.y) + 2u;
    if(fullVoxOffset.z > fullVoxDim.z)
        tpb.z =  fullVoxDim.z - (voxOffset.z - offsetToBrickBorder.z) + 2u;
    numThreadsPerBlock = dim3(tpb.x, tpb.y, tpb.z);
    //dim3 numThreadsPerBlock(128u, 128u, 128u);
    if(numThreadsPerBlock.x > static_cast<glm::uint>(_maxBlockDim.x) || 
       numThreadsPerBlock.y > static_cast<glm::uint>(_maxBlockDim.y) || 
       numThreadsPerBlock.z > static_cast<glm::uint>(_maxBlockDim.z))
    {
        numThreadsPerBlock.x = glm::min(numThreadsPerBlock.x, static_cast<glm::uint>(_maxBlockDim.x));
        numThreadsPerBlock.y = glm::min(numThreadsPerBlock.y, static_cast<glm::uint>(_maxBlockDim.y));
        numThreadsPerBlock.z = glm::min(numThreadsPerBlock.z, static_cast<glm::uint>(_maxBlockDim.z));

        numBlocksPerGrid.x = 
            static_cast<size_t>(glm::ceil((static_cast<float>(tpb.x) / static_cast<float>(numThreadsPerBlock.x))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.x))));
        numBlocksPerGrid.y = 
            static_cast<size_t>(glm::ceil((static_cast<float>(tpb.y) / static_cast<float>(numThreadsPerBlock.y))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.y))));
        numBlocksPerGrid.z = 
            static_cast<size_t>(glm::ceil((static_cast<float>(tpb.z) / static_cast<float>(numThreadsPerBlock.z))));
            //static_cast<size_t>(glm::ceil((static_cast<float>(128u) / static_cast<float>(numThreadsPerBlock.z))));
    }

    size_t numKernExecutions = 1;
    int kernXOffset = 0;
    if(tpb.x > 128u)
    {
        numBlocksPerGrid.x >>= 1;
        if(numBlocksPerGrid.x & 1 != 0)
            numBlocksPerGrid.x += 1;
        //numBlocksPerGrid.y >>= 1;
        //numBlocksPerGrid.z >>= 1;
        numKernExecutions = 2;
    }

    //had issue with launch timeout, so having to break this up
    //into two executions to get work around
    for(size_t kernExeIdx = 0u; kernExeIdx < numKernExecutions; ++kernExeIdx)
    {
        ComputeOctTreeNodeType<<<numBlocksPerGrid, numThreadsPerBlock>>>(voxelColors, 
                                                                         pOctTreeNodesDevPtr,
                                                                         pOctTreeNodesConstColorPtr,
                                                                         voxOffset,
                                                                         glm::ivec3(offsetToBrickBorder.x,
                                                                                    offsetToBrickBorder.y,
                                                                                    offsetToBrickBorder.z),
                                                                         //brickSampleOffset,
                                                                         glm::ivec3(mipMapChunkDim.x,
                                                                                    mipMapChunkDim.y,
                                                                                    mipMapChunkDim.z),
                                                                         fullVoxDim,
                                                                         octTreeNodesDim,
                                                                         _brickDim,
                                                                         kernXOffset);
        s_cudaStatus = cudaDeviceSynchronize();
        if(s_cudaStatus != cudaSuccess)
        {
            _error << "computeOctTreeNodes() error: " << s_cudaStatus << "." << std::endl;
            return;
        }

        kernXOffset += (numBlocksPerGrid.x * numThreadsPerBlock.x);
    }

    const VoxelColorMipMap& voxelColorMM = voxelColorMipMaps[voxelColorMipMaps.size() - depth - 1u];
    const VoxelNormalMipMap& voxelNormalMM = voxelNormalMipMaps[voxelNormalMipMaps.size() - depth - 1u];
    VoxelBrickWriter& brickWriter = voxelBrickWriters[depth];
    
    size_t rowReadSize = nodeXEnd - nodeXStart + 1u;  
    
    int nodeBaseX = (nodeXStart * _brickDim.x) - xOffset + offsetToBrickBorder.x;
    int nodeBaseY = (nodeYStart * _brickDim.y) - yOffset + offsetToBrickBorder.y;
    int nodeBaseZ = (nodeZStart * _brickDim.z) - zOffset + offsetToBrickBorder.z;

    //std::cout << std::endl << "Writing bricks";
    //TODO if nothing voxelized then replace this function with just a loop like the one
    //below that just sets all nodes to constant unless it is already set to non-const
    //int loopCount = 0;
    int mipMapZOffset = nodeBaseZ;
    for(size_t nodeZ = nodeZStart; 
        nodeZ <= nodeZEnd; 
        ++nodeZ,
        mipMapZOffset += _brickDim.z)
    {
        //if(loopCount++ % 3 == 0)
        //    std::cout << ".";
        int mipMapYOffset = nodeBaseY;
        for(size_t nodeY = nodeYStart;
            nodeY <= nodeYEnd;
            ++nodeY,
            mipMapYOffset += _brickDim.y)
        {
            size_t rowReadIndex = (nodeZ * octTreeNodesDim.y * octTreeNodesDim.x)
                                 + (nodeY * octTreeNodesDim.x) 
                                 + nodeXStart;
            size_t rowWriteIndex = writeIndex + rowReadIndex;
            s_cudaStatus = cudaMemcpy(&octTreeNodes[rowWriteIndex], 
                                      &pOctTreeNodesDevPtr[rowReadIndex], 
                                      sizeof(Voxelizer::OctTreeNode) * rowReadSize,
                                      cudaMemcpyDeviceToHost);
            if(s_cudaStatus != cudaSuccess)
            {
                _error << "computeOctTreeNodes() error: " << s_cudaStatus << "." << std::endl;
                return;
            }

            s_cudaStatus = cudaMemcpy(&octTreeConstColors[rowWriteIndex], 
                                      &pOctTreeNodesConstColorPtr[rowReadIndex], 
                                      sizeof(cuda::VoxColor) * rowReadSize,
                                      cudaMemcpyDeviceToHost);
            if(s_cudaStatus != cudaSuccess)
            {
                _error << "computeOctTreeNodes() error: " << s_cudaStatus << "." << std::endl;
                return;
            }

            int mipMapXOffset = nodeBaseX;
            for(size_t nodeX = nodeXStart; 
                nodeX <= nodeXEnd; 
                ++nodeX, 
                mipMapXOffset += _brickDim.x)
            {
                size_t curNodeIndex = rowWriteIndex + (nodeX - nodeXStart);
                    
                //compute x,y,z of first element of this node's brick
                if(mipMapXOffset + _brickDim.x >= mipMapChunkDim.x || mipMapXOffset <= 0 ||
                   mipMapYOffset + _brickDim.y >= mipMapChunkDim.y || mipMapYOffset <= 0 ||
                   mipMapZOffset + _brickDim.z >= mipMapChunkDim.z || mipMapZOffset <= 0)
                {
                    unsigned int brickX = 1u;
                    size_t mmXOffset = mipMapXOffset;
                    size_t brickSizeX = glm::min(_brickDim.x, mipMapChunkDim.x - mipMapXOffset);
                    if(mipMapXOffset < 0)
                    {
                        brickX = 0u - mipMapXOffset + 1u;
                        mmXOffset = 0u;
                        brickSizeX = _brickDim.x - brickX + 1u;
                    }

                    unsigned int brickY = 1u;
                    size_t mmYOffset = mipMapYOffset;
                    size_t brickSizeY = glm::min(_brickDim.y, mipMapChunkDim.y - mipMapYOffset);
                    if(mipMapYOffset < 0)
                    {
                        brickY = 0u - mipMapYOffset + 1u;
                        mmYOffset = 0u;
                        brickSizeY = _brickDim.y - brickY + 1u;
                    }
                    
                    unsigned int brickZ = 1u;
                    size_t mmZOffset = mipMapZOffset;
                    size_t brickSizeZ = glm::min(_brickDim.z, mipMapChunkDim.z - mipMapZOffset);
                    if(mipMapZOffset < 0)
                    {
                        brickZ = 0u - mipMapZOffset + 1u;
                        mmZOffset = 0u;
                        brickSizeZ = _brickDim.z - brickZ + 1u;
                    }

                    brickWriter.storePartialBrick(nodeX, nodeY, nodeZ,
                                                  brickX, brickY, brickZ,
                                                  mmXOffset, 
                                                  mmYOffset, 
                                                  mmZOffset,
                                                  brickSizeX,
                                                  brickSizeY,
                                                  brickSizeZ,
                                                  _brickDim.x, _brickDim.y, _brickDim.z,
                                                  voxelColorMM.colors, voxelNormalMM.normals,
                                                  voxelColorMM.dim.x, voxelColorMM.dim.y, voxelColorMM.dim.z);
                }
                else
                {
#ifdef __DEBUG__
                    if(!ValidateNode(mipMapXOffset, mipMapYOffset, mipMapZOffset,
                                     voxelColorMM.dim.x, voxelColorMM.dim.y, voxelColorMM.dim.z,
                                     _brickDim,
                                     voxelColorMM.colors,
                                     octTreeNodes[curNodeIndex]))
                    {
                        _error << "Failed to validate node." << std::endl;
                    }
#endif
                    if(octTreeNodes[curNodeIndex] == 1u)
                    {
                        if(!brickWriter.writeBrick(nodeX, nodeY, nodeZ,
                                                static_cast<size_t>(mipMapXOffset), 
                                                static_cast<size_t>(mipMapYOffset), 
                                                static_cast<size_t>(mipMapZOffset),
                                                voxelColorMM.dim.x, voxelColorMM.dim.y, voxelColorMM.dim.z,
                                                _brickDim.x, _brickDim.y, _brickDim.z,
                                                voxelColorMM.colors, voxelNormalMM.normals))
                        {
                            _error << "Failed to write brick data." << std::endl;
                        }

                    }
                }
            }
        }
    }

    //std::cout << std::endl;

    if(depth == 0)
        return;
    --depth;

    glm::uvec3 nextMipMapDim(mipMapChunkDim.x >> 1u,
                             mipMapChunkDim.y >> 1u,
                             mipMapChunkDim.z >> 1u);

    glm::uvec3 nextOffsetToBrickBorder(glm::max(offsetToBrickBorder.x >> 1u, 1u),//there is at least a one voxel border
                                       glm::max(offsetToBrickBorder.y >> 1u, 1u),
                                       glm::max(offsetToBrickBorder.z >> 1u, 1u));

    size_t nextNumNodes = static_cast<size_t>(glm::pow(8.0f, static_cast<float>(depth)));
    size_t nextXOffset = xOffset >> 1;
    size_t nextYOffset = yOffset >> 1;
    size_t nextZOffset = zOffset >> 1;
    computeOctTreeNodes(nextXOffset, nextYOffset, nextZOffset,
                        voxelDeviceMipMaps,
                        voxelColorMipMaps,
                        voxelNormalMipMaps,
                        voxelBrickWriters,
                        nextMipMapDim,
                        nextOffsetToBrickBorder,
                        octTreeDevBuffers,
                        octTreeNodes,
                        octTreeConstColors,
                        writeIndex - nextNumNodes,
                        depth);
}

bool Voxelizer::allocateDeviceMipMaps(const glm::uvec3& voxChunkDim,
                                     cudaPitchedPtr& voxelColorsDevPtr,
                                     cudaPitchedPtr& voxelNormalsDevPtr,
                                     VoxelDeviceMipMaps& voxelDeviceMipMaps)
{
    cudaExtent voxExt = make_cudaExtent(voxChunkDim.x * sizeof(cuda::VoxColor),
                                        voxChunkDim.y,
                                        voxChunkDim.z);
    
    //allocate gpu memory to hold voxel colors
    s_cudaStatus = cudaMalloc3D(&voxelColorsDevPtr, voxExt);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (voxExt.width * voxExt.height * voxExt.depth);

    s_cudaStatus = cudaMemset(voxelColorsDevPtr.ptr, 0, 
                              voxelColorsDevPtr.pitch * voxExt.height * voxExt.depth);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    //allocate gpu memory to hold voxel normals
    voxExt.width = voxChunkDim.x * sizeof(cuda::VoxNorm);
    s_cudaStatus = cudaMalloc3D(&voxelNormalsDevPtr, voxExt);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (voxExt.width * voxExt.height * voxExt.depth);

    s_cudaStatus = cudaMemset(voxelNormalsDevPtr.ptr, 0, 
                              voxelNormalsDevPtr.pitch * voxExt.height * voxExt.depth);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceMipMaps() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    
    //mip maps stored on device
    voxelDeviceMipMaps.push_back(VoxelDataPtrs(voxelColorsDevPtr, voxelNormalsDevPtr));

    if(!allocateDeviceMipMaps(voxChunkDim.x, 
                              voxChunkDim.y, 
                              voxChunkDim.z, 
                              voxelDeviceMipMaps))
    {
        return false;
    }

    return true;
}

bool Voxelizer::allocateDeviceVoxelizationMemory(const glm::uvec3& voxChunkDim,
                                                cudaPitchedPtr& voxelTriIndicesDevPtr,
                                                cudaPitchedPtr& voxelTriCountsDevPtr)
{
    cudaExtent voxExt = make_cudaExtent(voxChunkDim.x * sizeof(glm::lowp_ivec4),
                                        voxChunkDim.y,
                                        voxChunkDim.z);
    
    //allocate gpu memory to hold voxel colors
    s_cudaStatus = cudaMalloc3D(&voxelTriIndicesDevPtr, voxExt);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (voxelTriIndicesDevPtr.pitch * voxExt.height * voxExt.depth);

    s_cudaStatus = cudaMemset(voxelTriIndicesDevPtr.ptr, -1, 
                              voxelTriIndicesDevPtr.pitch * voxExt.height * voxExt.depth);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    //allocate gpu memory to hold voxel normals
    /*voxExt.width = voxChunkDim.x * sizeof(cuda::VoxNorm);
    s_cudaStatus = cudaMalloc3D(&voxelNormalsDevPtr, voxExt);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (voxExt.width * voxExt.height * voxExt.depth);

    s_cudaStatus = cudaMemset(voxelNormalsDevPtr.ptr, 0, 
                              voxelNormalsDevPtr.pitch * voxExt.height * voxExt.depth);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }*/
    
    //allocate gpu memory to hold voxel tri counts
    voxExt.width = voxChunkDim.x * sizeof(glm::uint);
    s_cudaStatus = cudaMalloc3D(&voxelTriCountsDevPtr, voxExt);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }
    s_totalAllocatedDeviceMemory += (voxelTriCountsDevPtr.pitch * voxExt.height * voxExt.depth);

    s_cudaStatus = cudaMemset(voxelTriCountsDevPtr.ptr, 0,
                              voxelTriCountsDevPtr.pitch * voxExt.height * voxExt.depth);
    if(s_cudaStatus != cudaSuccess)
    {
        _error << "allocateDeviceVoxelizationMemory() error: " << s_cudaStatus << "." << std::endl;
        cudaFree(voxelTriCountsDevPtr.ptr);
        return false;
    }

    return true;
}

//static void PrintVoxelColors(const VoxelColors& voxelColors, const glm::uvec3& voxDim)
//{
//    for(size_t z = 0; z < voxDim.z; ++z)
//    {
//        for(size_t y = 0; y < voxDim.y; ++y)
//        {
//            for(size_t x = 0; x < voxDim.x; ++x)
//            {
//                const cuda::VoxColor& color = voxelColors[(z * voxDim.y * voxDim.x) + (y * voxDim.x) + x];
//                if(color.w > 0.0f)
//                {
//                    std::cout << "Color at [" 
//                              << x << ", " << y << "," << z 
//                              << "]="
//                              << "[" 
//                              << color.x << ", " << color.y << ", " << color.z << ", " << color.w 
//                              << "]" 
//                              << std::endl;
//                }
//            }
//        }
//    }
//}

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

static inline bool VEC3_EQUAL(const glm::vec3& v1, const glm::vec3& v2)
{
    static float epsilon = 0.001f;
    glm::vec3 diff = v1 - v2;

    return (diff.x > -epsilon && diff.x < epsilon) &&
           (diff.y > -epsilon && diff.y < epsilon) &&
           (diff.z > -epsilon && diff.z < epsilon);
}

static inline bool VEC3_EQUAL(const uchar3& v1, const uchar3& v2)
{
    return v1.x == v2.x && v1.y == v2.y && v1.z == v2.z;
}

float VectorLenSq(const glm::vec3& vec)
{
     return vec.x * vec.x + vec.y * vec.y + vec.z * vec.z;
}

//static bool ValidateVoxelMipMaps(const VoxelColorMipMaps& voxelColorMipMaps,
//                                 const VoxelNormalMipMaps& voxelNormalMipMaps,
//                                 const glm::uvec3& voxDim,
//                                 size_t numMipMapLevels,
//                                 size_t curDepth=0)
//{
//    const VoxelColors& curLevelColors = voxelColorMipMaps.at(curDepth).colors;
//    const VoxelNormals& curLevelNormals = voxelNormalMipMaps.at(curDepth).normals;
//
//    const VoxelColors& nxtLevelColors = voxelColorMipMaps.at(curDepth+1).colors;
//    const VoxelNormals& nxtLevelNormals = voxelNormalMipMaps.at(curDepth+1).normals;
//
//    glm::uvec3 mmDim(voxDim);
//    mmDim.x >>= 1;
//    mmDim.y >>= 1;
//    mmDim.z >>= 1;
//
//    glm::uint xScale = voxDim.x / mmDim.x;
//    glm::uint yScale = voxDim.y / mmDim.y;
//    glm::uint zScale = voxDim.z / mmDim.z;
//
//    for(glm::uint z = 0; z < mmDim.z; ++z)
//    {
//        glm::uint baseZ = z * zScale;
//        glm::uint baseZPlus1 = baseZ + 1;
//        for(glm::uint y = 0; y < mmDim.y; ++y)
//        {
//            glm::uint baseY = y * yScale;
//            glm::uint baseYPlus1 = baseY + 1;
//            for(glm::uint x = 0; x < mmDim.x; ++x)
//            {
//                glm::uint baseX = x * xScale;
//                glm::uint baseXPlus1 = baseX + 1;
//    
//                const cuda::VoxColor& color = nxtLevelColors[(z * mmDim.y * mmDim.x) + (y * mmDim.x) + x];
//                const cuda::VoxNorm& normal = nxtLevelNormals[(z * mmDim.y * mmDim.x) + (y * mmDim.x) + x];
//
//                cuda::VoxColor colorBox[8] = {
//                    curLevelColors[(baseZ * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseX],//x, y, z
//                    curLevelColors[(baseZ * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseXPlus1],//x+1, y, z
//                    curLevelColors[(baseZ * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseX],//x, y+1, z
//                    curLevelColors[(baseZ * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseXPlus1],//x+1, y+1, z
//                    curLevelColors[(baseZPlus1 * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseX],//x, y, z+1
//                    curLevelColors[(baseZPlus1 * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseXPlus1],//x+1, y, z+1 
//                    curLevelColors[(baseZPlus1 * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseX],//x, y+1, z+1
//                    curLevelColors[(baseZPlus1 * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseXPlus1]//x+1, y+1, z+1
//                };
//
//                float alphaSum = colorBox[0].w;
//                alphaSum += colorBox[1].w;
//                alphaSum += colorBox[2].w;
//                alphaSum += colorBox[3].w;
//                alphaSum += colorBox[4].w;
//                alphaSum += colorBox[5].w;
//                alphaSum += colorBox[6].w;
//                alphaSum += colorBox[7].w;
//
//                float alphaWeights[8] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
//                if(alphaSum > 0.0f)
//                {
//                    alphaWeights[0] = colorBox[0].w / alphaSum;
//                    alphaWeights[1] = colorBox[1].w / alphaSum;
//                    alphaWeights[2] = colorBox[2].w / alphaSum;
//                    alphaWeights[3] = colorBox[3].w / alphaSum;
//                    alphaWeights[4] = colorBox[4].w / alphaSum;
//                    alphaWeights[5] = colorBox[5].w / alphaSum;
//                    alphaWeights[6] = colorBox[6].w / alphaSum;
//                    alphaWeights[7] = colorBox[7].w / alphaSum;
//                }
//
//                cuda::VoxColor avgColor;
//                cuda::VoxColor curColor4;
//                glm::vec3 curColor3;
//                curColor4 = colorBox[0];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[0];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[1]; 
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[1];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[2];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[2];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[3];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[3];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[4];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[4];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[5];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[5];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[6];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[6];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                curColor4 = colorBox[7];
//                curColor3 = glm::vec3(curColor4.x, curColor4.y, curColor4.z);
//                curColor3 *= alphaWeights[7];//glm::pow(curColor4.w, 2.0f);
//                avgColor += cuda::VoxColor(curColor3, curColor4.w);
//
//                float oneOverEight = (1.0f / 8.0f);
//                //avgColor.x *= oneOverEight;
//                //avgColor.y *= oneOverEight;
//                //avgColor.z *= oneOverEight;
//                avgColor.w *= oneOverEight;//glm::min(avgColor.w, 1.0f);
//                avgColor.w = glm::pow(avgColor.w, 1.0f - avgColor.w);
//                avgColor.x *= avgColor.w;
//                avgColor.y *= avgColor.w;
//                avgColor.z *= avgColor.w;
//
//                if(VEC4_EQUAL(avgColor, color) == false)
//                    return false;
//
//                cuda::VoxNorm avgNormal;
//                avgNormal  = curLevelNormals[(baseZ * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseX] 
//                            * alphaWeights[0];//colorBox[0].w;//x, y, z
//                avgNormal += curLevelNormals[(baseZ * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseXPlus1]
//                            * alphaWeights[1];//colorBox[1].w;//x+1, y, z 
//                avgNormal += curLevelNormals[(baseZ * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseX]
//                            * alphaWeights[2];//colorBox[2].w;//x, y+1, z
//                avgNormal += curLevelNormals[(baseZ * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseXPlus1]
//                            * alphaWeights[3];//colorBox[3].w;//x+1, y+1, z
//
//                avgNormal += curLevelNormals[(baseZPlus1 * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseX]
//                            * alphaWeights[4];//colorBox[4].w;//x, y, z+1
//                avgNormal += curLevelNormals[(baseZPlus1 * voxDim.y * voxDim.x) + (baseY * voxDim.x) + baseXPlus1]
//                            * alphaWeights[5];//colorBox[5].w;//x+1, y, z+1 
//                avgNormal += curLevelNormals[(baseZPlus1 * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseX]
//                            * alphaWeights[6];//colorBox[6].w;//x, y+1, z+1
//                avgNormal += curLevelNormals[(baseZPlus1 * voxDim.y * voxDim.x) + (baseYPlus1 * voxDim.x) + baseXPlus1]
//                            * alphaWeights[7];//colorBox[7].w;//x+1, y+1, z+1
//
//                //avgNormal *= oneOverEight;
//                float len = glm::length(avgNormal);
//                if(len > 0.0f)
//                    avgNormal /= len;
//                else
//                    avgNormal = cuda::VoxNorm(0);
//
//                if(VEC3_EQUAL(avgNormal, normal) == false)
//                    return false;
//            }
//        }
//    }
//
//    ++curDepth;
//    if(curDepth+1 == numMipMapLevels)
//    {
//        return true;
//    }
//
//    return ValidateVoxelMipMaps(voxelColorMipMaps, voxelNormalMipMaps, mmDim, numMipMapLevels, curDepth);
//}

//static void CopyRootBrickChunkToRootBrick(glm::uint xOffset, glm::uint yOffset, glm::uint zOffset,
//                                     const VoxelColorMipMap& chunkColors,
//                                     const VoxelNormalMipMap& chunkNormals,
//                                     VoxelColorMipMap& rootBrickColors,
//                                     VoxelNormalMipMap& rootBrickNormals)
//{
//    
//    for(glm::uint zRead = 0u; zRead < chunkColors.dim.z; ++zRead)
//    {
//        for(glm::uint yRead = 0u; yRead < chunkColors.dim.y; ++yRead)
//        {
//            glm::uint readIndex = (zRead * chunkColors.dim.y * chunkColors.dim.x)
//                                  + (yRead * chunkColors.dim.x);
//
//            glm::uint writeIndex = ((zOffset + zRead) * rootBrickColors.dim.y * rootBrickColors.dim.x)
//                           + ((yOffset + yRead) * rootBrickColors.dim.x)
//                           + xOffset;
//
//            memcpy(&rootBrickColors.colors[writeIndex],
//                   &chunkColors.colors[readIndex],
//                   sizeof(cuda::VoxColor) * chunkColors.dim.x);
//            
//            memcpy(&rootBrickNormals.normals[writeIndex],
//                   &chunkNormals.normals[readIndex],
//                   sizeof(cuda::VoxNorm) * chunkNormals.dim.x);
//        }
//    }
//}

bool Voxelizer::initVoxelBrickWriters(const std::string& outputDir, 
                                      bool outputBinary,
                                      bool outputCompressed,
                                      VoxelBrickWriters& voxelBrickWriters)
{
    std::string voxelMipMapPrefix = "voxels_";
    for(size_t i = 0; i < _voxelColorMipMaps.size(); ++i)
    {
        std::stringstream voxelOutputFile;
        voxelOutputFile << outputDir << "/" << voxelMipMapPrefix << i;

        if(i == 0 && 
           outputBinary && 
           voxelBrickWriters.at(i).createOpenGLContext() == false)
        {
            _error << "Failed to create compression context for compressed output." << std::endl;
            return false;
        }

        std::stringstream outputPartialBricksDir;
        outputPartialBricksDir << outputDir << "/partial_bricks_" << i;
        if(!voxelBrickWriters.at(i).startBricksFile(outputPartialBricksDir.str(), voxelOutputFile.str(), outputBinary, outputCompressed))
        {
            _error << "Failed to open " << voxelOutputFile.str() << " for output." << std::endl;
            return false;
        }
    }

    return true;
}

bool WriteCompletedStoredBricks(VoxelBrickWriters& voxelBrickWriters,
                       const Voxelizer::OctTreeNodes& octTreeNodes,
                       size_t octTreeNodeStartIndex=0,
                       size_t depth=0)
{
    size_t numNodes = static_cast<size_t>(glm::pow(8.0f, static_cast<float>(depth)));
    size_t cubeRoot = static_cast<size_t>(glm::pow(static_cast<float>(numNodes), 1.0f / 3.0f));

    size_t index = octTreeNodeStartIndex;
    const glm::uint* pOctTreeNodes = &octTreeNodes[index];
    
    if(!voxelBrickWriters.at(depth).writeCompletedStoredBricks(pOctTreeNodes, cubeRoot, cubeRoot))
        return false;

    if(depth == voxelBrickWriters.size() - 1)
        return true;

    return WriteCompletedStoredBricks(voxelBrickWriters,
                             octTreeNodes,
                             octTreeNodeStartIndex + numNodes,
                             depth+1);
}

static void SvgCircle(std::ofstream& svg,
                      float radius,
                      size_t x, size_t y, size_t triIndex,
                      size_t voxX, size_t voxY, size_t voxZ)
{
    static std::string colors[] = {
        "red", "green", "blue", 
        "darkred", "darkgreen", "darkblue",
        "gold", "gray", "deeppink",
        "darkslategray", "blueviolet", "brown", 
        "crimson", "hotpink", "yellow",
        "tomato", "seagreen", "purple"
    };
    size_t colorIndex = triIndex % 18;
    const std::string& color = colors[colorIndex];
    svg << "<circle stroke-width=\"1\" "
        << "r=\"" << radius << "\" "
        << "cx=\"" << x << "\" "
        << "cy=\"" << y << "\" "
        << "fill=\"" << color << "\" "
        << "id=\"" << triIndex << "\" "
        << "onclick=\"alert('" << voxX << ", " << voxY << ", " << voxZ << "')\" "
        << "/>" << std::endl;
}

static void SvgCircle(std::ofstream& svg,
                      float radius,
                      size_t x, size_t y,
                      const cuda::VoxColor& color,
                      size_t voxX, size_t voxY, size_t voxZ)
{
    svg << "<circle stroke-width=\"1\" "
        << "r=\"" << radius << "\" "
        << "cx=\"" << x << "\" "
        << "cy=\"" << y << "\" "
        << "fill=\"rgb("
        << static_cast<int>(sizeof(color) > 4 ? color.x * 255.0 : color.x) << ","
        << static_cast<int>(sizeof(color) > 4 ? color.y * 255.0 : color.y) << ","
        << static_cast<int>(sizeof(color) > 4 ? color.z * 255.0 : color.z) << "\" "
        << "opacity=\""
        << color.w
        << "\" "
        << "onclick=\"alert('" << voxX << ", " << voxY << ", " << voxZ << "')\" "
        << "/>" << std::endl;
}

static void SvgTriangle(std::ofstream& svg,
                        float x0, float y0,
                        float x1, float y1,
                        float x2, float y2,
                        size_t triIndex,
                        const glm::vec3& v0,
                        const glm::vec3& v1,
                        const glm::vec3& v2)
{
    svg << "<path stroke-width=\"0.25\" "
        << "d=\"M" 
        << x0 << " " 
        << y0 << "L" 
        << x1 << " " 
        << y1 << "L" 
        << x2 << " " 
        << y2 << " Z\" "
        << "stroke=\"black\" "
        << "id=\"" << triIndex << "\" "
        << "opacity=\"0.25\" fill=\"black\" "
        << "onclick=\"alert('v0=[" 
        << v0.x << ", " << v0.y << ", " << v0.z << "] v1=["
        << v1.x << ", " << v1.y << ", " << v1.z << "] v2=["
        << v2.x << ", " << v2.y << ", " << v2.z << "]')\""
        << "/>" << std::endl;
}

static void PrintSvgHeader(std::ofstream& svg)
{
    svg << "<svg" << std::endl;
    svg << "    xmlns:svg='http://www.w3.org/2000/svg'" << std::endl;
    svg << "    xmlns='http://www.w3.org/2000/svg'" << std::endl;
    svg << "    version='1.1'" << std::endl;
    svg << "    width='800'" << std::endl;
    svg << "    height='600'" << std::endl;
    svg << "    onload='init(evt)'>" << std::endl;
    svg << "   " << std::endl;
    svg << "  <style>" << std::endl;
    svg << "  		.territory:hover{" << std::endl;
    svg << "			fill:           #22aa44;" << std::endl;
    svg << "		}" << std::endl;
    svg << "  		.compass{" << std::endl;
    svg << "  			fill:			#fff;" << std::endl;
    svg << "  			stroke:			#000;" << std::endl;
    svg << "  			stroke-width:	1.5;" << std::endl;
    svg << "  		}" << std::endl;
    svg << "   		.button{" << std::endl;
    svg << "		    fill:           	#225EA8;" << std::endl;
    svg << "			stroke:   			#0C2C84;" << std::endl;
    svg << "			stroke-miterlimit:	6;" << std::endl;
    svg << "			stroke-linecap:		round;" << std::endl;
    svg << "		}" << std::endl;
    svg << "		.button:hover{" << std::endl;
    svg << "			stroke-width:   	2;" << std::endl;
    svg << "		}" << std::endl;
    svg << "		.plus-minus{" << std::endl;
    svg << "			fill:	#fff;" << std::endl;
    svg << "			pointer-events: none;" << std::endl;
    svg << "		}" << std::endl;
    svg << "  </style>" << std::endl;
    svg << "  " << std::endl;
    svg << "  <script type='text/ecmascript'>" << std::endl;
    svg << "    <![CDATA[" << std::endl;
    svg << "		var transMatrix = [2,0,0,2,0,0];" << std::endl;
    svg << "        " << std::endl;
    svg << "        function init(evt)" << std::endl;
    svg << "        {" << std::endl;
    svg << "            if ( window.svgDocument == null )" << std::endl;
    svg << "            {" << std::endl;
    svg << "                svgDoc = evt.target.ownerDocument;" << std::endl;
    svg << "" << std::endl;
    svg << "            }" << std::endl;
    svg << "            mapMatrix = svgDoc.getElementById('map-matrix');" << std::endl;
    svg << "            width  = evt.target.getAttributeNS(null, 'width');" << std::endl;
    svg << "            height = evt.target.getAttributeNS(null, 'height');" << std::endl;
    svg << "        }" << std::endl;
    svg << "        " << std::endl;
    svg << "        function pan(dx, dy)" << std::endl;
    svg << "        {" << std::endl;
    svg << "        	" << std::endl;
    svg << "			transMatrix[4] += dx;" << std::endl;
    svg << "			transMatrix[5] += dy;" << std::endl;
    svg << "            " << std::endl;
    svg << "			newMatrix = 'matrix(' +  transMatrix.join(' ') + ')';" << std::endl;
    svg << "			mapMatrix.setAttributeNS(null, 'transform', newMatrix);" << std::endl;
    svg << "        }" << std::endl;
    svg << "        " << std::endl;
    svg << "		function zoom(scale)" << std::endl;
    svg << "		{" << std::endl;
    svg << "			for (var i=0; i<transMatrix.length; i++)" << std::endl;
    svg << "			{" << std::endl;
    svg << "				transMatrix[i] *= scale;" << std::endl;
    svg << "			}" << std::endl;
    svg << "			transMatrix[4] += (1-scale)*width/2;" << std::endl;
    svg << "			transMatrix[5] += (1-scale)*height/2;" << std::endl;
    svg << "		        " << std::endl;
    svg << "			newMatrix = 'matrix(' +  transMatrix.join(' ') + ')';" << std::endl;
    svg << "			mapMatrix.setAttributeNS(null, 'transform', newMatrix);" << std::endl;
    svg << "        }" << std::endl;
    svg << "    ]]>" << std::endl;
    svg << "  </script>" << std::endl;
    svg << "  <g id='map-matrix' transform='matrix(2 0 0 2 0 0)'>" << std::endl;
}

static void PrintSvgFooter(std::ofstream& svg)
{
    svg << "  </g>" << std::endl; 
    svg << "  <circle cx='50' cy='50' r='42' fill='white' opacity='0.75'/>" << std::endl;
    svg << "  <path class='button' onclick='pan( 0, 50)' d='M50 10 l12   20 a40, 70 0 0,0 -24,  0z' />" << std::endl;
    svg << "  <path class='button' onclick='pan( 50, 0)' d='M10 50 l20  -12 a70, 40 0 0,0   0, 24z' />" << std::endl;
    svg << "  <path class='button' onclick='pan( 0,-50)' d='M50 90 l12  -20 a40, 70 0 0,1 -24,  0z' />" << std::endl;
    svg << "  <path class='button' onclick='pan(-50, 0)' d='M90 50 l-20 -12 a70, 40 0 0,1   0, 24z' />" << std::endl;
  
    svg << "  <circle class='compass' cx='50' cy='50' r='20'/>" << std::endl;
    svg << "  <circle class='button'  cx='50' cy='41' r='8' onclick='zoom(0.8)'/>" << std::endl;
    svg << "  <circle class='button'  cx='50' cy='59' r='8' onclick='zoom(1.25)'/>" << std::endl;

    svg << "  <rect class='plus-minus' x='46' y='39.5' width='8' height='3'/>" << std::endl;
    svg << "  <rect class='plus-minus' x='46' y='57.5' width='8' height='3'/>" << std::endl;
    svg << "  <rect class='plus-minus' x='48.5' y='55' width='3' height='8'/>" << std::endl;
        
  svg << "</svg>" << std::endl;
}

static void ExportMipMapToSvg(const VoxelColors& voxelColors,
                              const glm::uvec3& voxDim,
                              const glm::vec3& voxOrigin,
                              const glm::vec3& voxScale,
                              const std::string& outputFileName)
{
    std::ofstream svgX, svgY, svgZ;
    svgX.open(outputFileName + ".x.svg");
    svgY.open(outputFileName + ".y.svg");
    svgZ.open(outputFileName + ".z.svg");
    
    PrintSvgHeader(svgX);
    PrintSvgHeader(svgY);
    PrintSvgHeader(svgZ);
    
    for(size_t z = 0; z < voxDim.z; ++z)
    {
        for(size_t y = 0; y < voxDim.y; ++y)
        {
            for(size_t x = 0; x < voxDim.x; ++x)
            {
                const cuda::VoxColor& color = voxelColors.at((z * voxDim.x * voxDim.y) + (y * voxDim.x) + x);
                float radius = 0.25f;
                if(color.w > 0.0f)
                {
                    SvgCircle(svgX, radius, z, y, color, x, y, z);
                    SvgCircle(svgY, radius, x, z, color, x, y, z);
                    SvgCircle(svgZ, radius, x, y, color, x, y, z);
                }
            }
        }
    }
    
    svgX << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"" << voxDim.z << "\" "
         << "height=\"" << voxDim.y << "\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;
    svgY << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"" << voxDim.x << "\" "
         << "height=\"" << voxDim.z << "\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;
    svgZ << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"" << voxDim.x << "\" "
         << "height=\"" << voxDim.y << "\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;

    PrintSvgFooter(svgX);
    PrintSvgFooter(svgY);
    PrintSvgFooter(svgZ);
}

static void ExportToSvg(const glm::uvec3& voxDim, 
                        size_t pitch,
                        const glm::lowp_ivec4* pTriIndicesBuf,
                        const std::vector<glm::vec3>* pTriVerts,
                        size_t triGroupOffset,
                        const glm::vec3& voxOrigin,
                        const glm::vec3& voxScale,
                        const std::string& outputFileName)
{   
    std::ofstream svgX, svgY, svgZ;
    svgX.open(outputFileName + ".x.svg");
    svgY.open(outputFileName + ".y.svg");
    svgZ.open(outputFileName + ".z.svg");
    
    PrintSvgHeader(svgX);
    PrintSvgHeader(svgY);
    PrintSvgHeader(svgZ);

    for(size_t vertIndex = 0;
        vertIndex < pTriVerts->size();
        vertIndex += 3)
    {
        size_t triIndex = static_cast<size_t>(static_cast<float>(vertIndex) / 3.0f);
        glm::vec3 v00 = pTriVerts->at(vertIndex);
        glm::vec3 v11 = pTriVerts->at(vertIndex+1);
        glm::vec3 v22 = pTriVerts->at(vertIndex+2);
        glm::vec3 v0 = ((v00 - voxOrigin) * voxScale.x);
        glm::vec3 v1 = ((v11 - voxOrigin) * voxScale.y);
        glm::vec3 v2 = ((v22 - voxOrigin) * voxScale.z);
        /*if((v0.x >= 0.0f && static_cast<unsigned int>(v0.x) < voxDim.x &&
            v0.y >= 0.0f && static_cast<unsigned int>(v0.y) < voxDim.y &&
            v0.z >= 0.0f && static_cast<unsigned int>(v0.z) < voxDim.z) ||
           (v1.x >= 0.0f && static_cast<unsigned int>(v1.x) < voxDim.x &&
            v1.y >= 0.0f && static_cast<unsigned int>(v1.y) < voxDim.y &&
            v1.z >= 0.0f && static_cast<unsigned int>(v1.z) < voxDim.z) ||
           (v2.x >= 0.0f && static_cast<unsigned int>(v2.x) < voxDim.x &&
            v2.y >= 0.0f && static_cast<unsigned int>(v2.y) < voxDim.y &&
            v2.z >= 0.0f && static_cast<unsigned int>(v2.z) < voxDim.z))*/
        {
            SvgTriangle(svgX, v0.z, v0.y, v1.z, v1.y, v2.z, v2.y, triIndex, 
                            v00, v11, v22);
            SvgTriangle(svgY, v0.x, v0.z, v1.x, v1.z, v2.x, v2.z, triIndex,
                            v00, v11, v22);
            SvgTriangle(svgZ, v0.x, v0.y, v1.x, v1.y, v2.x, v2.y, triIndex,
                            v00, v11, v22);
        }
    }
    
    std::set<size_t> triangles;
    char* pReadPtrTriIndices = (char*)pTriIndicesBuf;
    size_t triIndicesSlicePitch = pitch * voxDim.y;
    for(size_t z = 0; z < voxDim.z; ++z)
    {
        char* pSliceReadTriIndices = pReadPtrTriIndices + z * triIndicesSlicePitch;
        for(size_t y = 0; y < voxDim.y; ++y)
        {
            glm::lowp_ivec4* pVoxelReadTriIndices = 
                (glm::lowp_ivec4*)(pSliceReadTriIndices + y * pitch);
            for(size_t x = 0; x < voxDim.x; ++x)
            {
                const glm::lowp_ivec4& indices = pVoxelReadTriIndices[x];
                for(size_t i = 0; i < 4 && indices[i] != -1; ++i)
                {
                    size_t triIndex = indices[i] - triGroupOffset;
                    float radius = (1.0f / static_cast<float>(i+1)) * 0.5f;
                    SvgCircle(svgX, radius, z, y, triIndex, x, y, z);
                    SvgCircle(svgY, radius, x, z, triIndex, x, y, z);
                    SvgCircle(svgZ, radius, x, y, triIndex, x, y, z);
                    triangles.insert(triIndex);
                }
            }
        }
    }
    
    svgX << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"255\" "
         << "height=\"255\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;
    svgY << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"255\" "
         << "height=\"255\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;
    svgZ << "<rect "
         << "x=\"0\" "
         << "y=\"0\" "
         << "width=\"255\" "
         << "height=\"255\" "
         << "stroke-width=\"1\" "
         << "fill=\"none\" "
         << "stroke=\"black\" "
         << "opacity=\"0.5\" />"
         << std::endl;

    PrintSvgFooter(svgX);
    PrintSvgFooter(svgY);
    PrintSvgFooter(svgZ);
}

static void ExportToScalarFileHeader(std::ofstream& voxFile,
                                     size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                     const std::string& headerFileName)
{
    voxFile.open(headerFileName, std::fstream::out);
    if(!voxFile.is_open())
    {
        return;
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
    voxFile << "TYPE SCALARS" << std::endl;
    voxFile << "FORMAT GL_R8" << std::endl;
    voxFile << "VOXEL_HEADER_END" << std::endl;
    voxFile << "VOXEL_SUB_IMAGE_FILES" << std::endl;
}

static void ExportToScalarFile(
                        std::ofstream& voxScalarHeaderFile,
                        const glm::uvec3& voxDim, 
                        size_t pitch,
                        size_t xStart, size_t yStart, size_t zStart,
                        size_t xEnd, size_t yEnd, size_t zEnd,
                        const glm::uint* pTriCountsBuf,
                        const std::string& outputFileName,
                        const glm::uvec3& subRangeStart,
                        const glm::uvec3& subRangeEnd)
{
    voxScalarHeaderFile << "VOXEL_SUB_IMAGE_FILE " 
                << subRangeStart.x << " " << subRangeStart.y << " " << subRangeStart.z << " "
                << subRangeEnd.x << " " << subRangeEnd.y << " " << subRangeEnd.z << " "
                << outputFileName
                << std::endl;

    std::ofstream voxOut;
    voxOut.open(outputFileName, std::ios_base::out | std::ios_base::binary);
    if(voxOut.is_open() == false)
        return;

    size_t xSize = xEnd - xStart;
    size_t ySize = yEnd - yStart;
    size_t zSize = zEnd - zStart;
    
    std::vector<unsigned char> voxelScalars(xSize * ySize * zSize);

    char* pReadPtrTriCounts = (char*)pTriCountsBuf;
    size_t triCountsSlicePitch = pitch * voxDim.y;
    size_t zWrite = 0;
    for(size_t z = zStart; z < zEnd; ++z, ++zWrite)
    {
        char* pSliceReadTriCounts = pReadPtrTriCounts + z * triCountsSlicePitch;
        size_t yWrite = 0;
        for(size_t y = yStart; y < yEnd; ++y, ++yWrite)
        {
            glm::uint* pVoxelReadTriCounts = 
                (glm::uint*)(pSliceReadTriCounts + y * pitch);
            size_t writeIndex = (zWrite * ySize * xSize) + (yWrite * xSize);
            unsigned char* pVoxelScalars = &voxelScalars[writeIndex];
            size_t xWrite = 0;
            for(size_t x = xStart; x < xEnd; ++x, ++xWrite)
            {
                const glm::uint& triCount = pVoxelReadTriCounts[x];
                if(triCount > 0)
                    pVoxelScalars[xWrite] = 255;
                else
                    pVoxelScalars[xWrite] = 0;
            }
        }
    }
    int formatIsUByte = 1;
    voxOut.write((const char*)&formatIsUByte, sizeof(formatIsUByte));
    unsigned int dataSize = sizeof(unsigned char)
                      * xSize * ySize * zSize;
    voxOut.write((const char*)&dataSize, sizeof(dataSize));
    
    voxOut.write((const char*)voxelScalars.data(), dataSize);
}

struct VoxelChunkID
{
    size_t xOffset; 
    size_t yOffset; 
    size_t zOffset;
    VoxelChunkID(size_t x, size_t y, size_t z) : 
        xOffset(x), yOffset(y), zOffset(z) {}
    bool operator<(const VoxelChunkID& rhs) const
    {
        return zOffset < rhs.zOffset || 
                (zOffset == rhs.zOffset && yOffset < rhs.yOffset) || 
                (zOffset == rhs.zOffset && yOffset == rhs.yOffset 
                    && xOffset < rhs.xOffset);
    }
};

typedef std::set<VoxelChunkID> QueuedVoxelChunks;

typedef std::list<VoxelChunkID> VoxelChunkQueue;

int Voxelizer::generateVoxelsAndOctTree(const std::string& outputDir, 
                                        bool outputBinary,
                                        bool outputCompressed)
{
    glm::uvec3 voxChunkDim = _voxChunkDim;
    //these two vectors will hold the chunks of color and normal texture after reading back from gpu
    VoxelColorMipMaps& voxelColorMipMaps = _voxelColorMipMaps;
    VoxelNormalMipMaps& voxelNormalMipMaps = _voxelNormalMipMaps;

    //allocate memory for voxel mip maps
    cudaPitchedPtr& voxelTriIndicesDevPtr = _voxelTriIndicesDevPtr;
    cudaPitchedPtr& voxelTriCountsDevPtr = _voxelTriCountsDevPtr;

    VoxelBrickWriters voxelBrickWriters(_voxelColorMipMaps.size());
    if(!initVoxelBrickWriters(outputDir, outputBinary, outputCompressed, voxelBrickWriters))
    {
        return false;
    }

    cudaPitchedPtr& voxelColorsDevMipMapPtr = _voxelColorsDevMipMapPtr;
    cudaPitchedPtr& voxelNormalsDevMipMapPtr = _voxelNormalsDevMipMapPtr;

    OctTreeDeviceBuffers& octTreeDeviceBuffers = _octTreeDeviceBuffers;
    if(!initOctTreeDeviceBuffers(_octTreeDeviceBuffers,
                                 voxelColorMipMaps.size()-1u))
    {
        return false;
    }

    OctTreeNodes& octTreeNodes = _octTreeNodes;
    octTreeNodes.assign(octTreeNodes.size(), 0u);
    OctTreeConstColors& octTreeConstColors = _octTreeConstColors;
    cuda::VoxColor zeros;
    zeros.x = zeros.y = zeros.z = zeros.w = 0;
    octTreeConstColors.assign(octTreeConstColors.size(), zeros);
     
    dim3 numBlocksPerGrid(1u, 1u, 1u);
    dim3 numThreadsPerBlock(voxChunkDim.x, voxChunkDim.y, voxChunkDim.z);
    if(numThreadsPerBlock.x > static_cast<glm::uint>(_maxBlockDim.x) || 
       numThreadsPerBlock.y > static_cast<glm::uint>(_maxBlockDim.y) || 
       numThreadsPerBlock.z > static_cast<glm::uint>(_maxBlockDim.z))
    {
        numThreadsPerBlock.x = glm::min(numThreadsPerBlock.x, static_cast<glm::uint>(_maxBlockDim.x));
        numThreadsPerBlock.y = glm::min(numThreadsPerBlock.y, static_cast<glm::uint>(_maxBlockDim.y));
        numThreadsPerBlock.z = glm::min(numThreadsPerBlock.z, static_cast<glm::uint>(_maxBlockDim.z));

        numBlocksPerGrid.x = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.x) / static_cast<float>(numThreadsPerBlock.x))));
        numBlocksPerGrid.y = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.y) / static_cast<float>(numThreadsPerBlock.y))));
        numBlocksPerGrid.z = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.z) / static_cast<float>(numThreadsPerBlock.z))));
    }

    dim3 threadsPerBlock(numThreadsPerBlock.x,
                         numThreadsPerBlock.y,
                         numThreadsPerBlock.z);

    dim3 computeCNBlocksPerGrid(1u, 1u, 1u);
    dim3 computeCNThreadsPerBlock(voxChunkDim.x, voxChunkDim.y, voxChunkDim.z);
    if(voxChunkDim.x > static_cast<glm::uint>(_maxBlockDim.x) || 
       voxChunkDim.y > static_cast<glm::uint>(_maxBlockDim.y) || 
       voxChunkDim.z > static_cast<glm::uint>(_maxBlockDim.z))
    {
        computeCNThreadsPerBlock.x = glm::min(voxChunkDim.x, static_cast<glm::uint>(_maxBlockDim.x));
        computeCNThreadsPerBlock.y = glm::min(voxChunkDim.y, static_cast<glm::uint>(_maxBlockDim.y));
        computeCNThreadsPerBlock.z = glm::min(voxChunkDim.z, static_cast<glm::uint>(_maxBlockDim.z));

        computeCNBlocksPerGrid.x = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.x) / static_cast<float>(computeCNThreadsPerBlock.x))));
        computeCNBlocksPerGrid.y = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.y) / static_cast<float>(computeCNThreadsPerBlock.y))));
        computeCNBlocksPerGrid.z = 
            static_cast<size_t>(glm::ceil((static_cast<float>(voxChunkDim.z) / static_cast<float>(computeCNThreadsPerBlock.z))));
    }

#ifdef __DEBUG__
    std::vector<std::string> debugVoxelColorFiles;
    bool debugFormatIsUByte = true;
    std::vector<glm::uvec3> debugVoxelColorStartRanges;
    std::vector<glm::uvec3> debugVoxelColorEndRanges;
    std::ofstream voxScalarFile;
    if(s_debug_output || s_debug_scalars)
    {
        std::stringstream scalarFileHeader;
        scalarFileHeader << outputDir << "/VoxelizerScalars.voxt";
        ExportToScalarFileHeader(voxScalarFile,
                                 _voxDim.x,
                                 _voxDim.y,
                                 _voxDim.z,
                                 scalarFileHeader.str());
    }
#endif

    bool atLeastOneTriGroupOverlapsVoxChunk = false;
    //use this for texture map
    unsigned char* pCurImage = nullptr;
    //iterate through entire highest res voxel mip map
    QueuedVoxelChunks queuedVoxelChunks;
    VoxelChunkQueue voxelChunkQueue;

    voxelChunkQueue.push_back(VoxelChunkID(0u, 0u, 0u));
    queuedVoxelChunks.insert(voxelChunkQueue.front());

    while(voxelChunkQueue.size() > 0)
    {
        VoxelChunkID voxelChunkID = voxelChunkQueue.front();
        voxelChunkQueue.pop_front();

        glm::uint x, y, z;
        x = voxelChunkID.xOffset;
        y = voxelChunkID.yOffset;
        z = voxelChunkID.zOffset;
        for(glm::uint nx = x; nx <= x + voxChunkDim.x; nx += voxChunkDim.x)
        {
            for(glm::uint ny = y; ny <= y + voxChunkDim.y; ny += voxChunkDim.y)
            {
                for(glm::uint nz = z; nz <= z + voxChunkDim.z; nz += voxChunkDim.z)
                {
                    VoxelChunkID neighborVoxelChunk(nx, ny, nz);
                    if(neighborVoxelChunk.xOffset < _voxDim.x
                        && neighborVoxelChunk.yOffset < _voxDim.y
                        && neighborVoxelChunk.zOffset < _voxDim.z
                        && queuedVoxelChunks.insert(neighborVoxelChunk).second == true)
                    {
                        voxelChunkQueue.push_back(neighborVoxelChunk);
                    }
                }
            }
        }
    
        glm::uvec3 minVoxChunk(x, y, z);
        glm::uvec3 maxVoxChunk(minVoxChunk.x + voxChunkDim.x - 1u,
                                minVoxChunk.y + voxChunkDim.y - 1u,
                                minVoxChunk.z + voxChunkDim.z - 1u);

        std::cout << "Voxelizing " 
                    << minVoxChunk.x << " " 
                    << minVoxChunk.y << " " 
                    << minVoxChunk.z << std::endl;

        glm::vec3 voxOffsetOrigin(_offsetP.x + (x * _deltaP.x),
                                    _offsetP.y + (y * _deltaP.y),
                                    _offsetP.z + (z * _deltaP.z));
                
        cudaMemset(voxelTriCountsDevPtr.ptr, 0,
                    voxelTriCountsDevPtr.pitch * voxChunkDim.y * voxChunkDim.z);

        cudaMemset(voxelColorsDevMipMapPtr.ptr, 0, 
                    voxelColorsDevMipMapPtr.pitch * voxChunkDim.y * voxChunkDim.z);

        cudaMemset(voxelNormalsDevMipMapPtr.ptr, 0,
                    voxelNormalsDevMipMapPtr.pitch * voxChunkDim.y * voxChunkDim.z);

        size_t triGroupOffset = 0;
        size_t uvOffset = 0;
        size_t normalsOffset = 0;
        for(TriangleGroups::iterator triGrp = _triangleGroups.begin();
            triGrp != _triangleGroups.end();
            ++triGrp)
        {
            bool trisOverlapVoxChunk = true;
            if((triGrp->voxelBBox.minVox.x > maxVoxChunk.x ||
                triGrp->voxelBBox.maxVox.x < minVoxChunk.x))
                trisOverlapVoxChunk = false;
            if((triGrp->voxelBBox.minVox.y > maxVoxChunk.y ||
                triGrp->voxelBBox.maxVox.y < minVoxChunk.y))
                trisOverlapVoxChunk = false;
            if((triGrp->voxelBBox.minVox.z > maxVoxChunk.z ||
                triGrp->voxelBBox.maxVox.z < minVoxChunk.z))
                trisOverlapVoxChunk = false;

            if(trisOverlapVoxChunk)
                atLeastOneTriGroupOverlapsVoxChunk = true;

            size_t numTris = triGrp->pVerts->size() / 3;

            dim3 numBlocks(numBlocksPerGrid.x,
                            numBlocksPerGrid.y,
                            static_cast<glm::uint>(numTris));

            size_t maxTrisPerVoxelization = 512u;
            size_t voxelizationBlockZDim = numTris;
            if(numTris > maxTrisPerVoxelization)
            {
                voxelizationBlockZDim = maxTrisPerVoxelization;
            }

            bool perVtxNormals = triGrp->pVertNormals != nullptr;
            for(size_t triOffset = triGroupOffset;
                trisOverlapVoxChunk && triOffset < (triGroupOffset + numTris);
                triOffset += maxTrisPerVoxelization)
            {
                if((triOffset-triGroupOffset) + voxelizationBlockZDim > numTris)
                    numBlocks.z = static_cast<glm::uint>(numTris - (triOffset-triGroupOffset));
                else
                    numBlocks.z = static_cast<glm::uint>(voxelizationBlockZDim);

                glm::uint zStart = glm::max(triGrp->voxelBBox.minVox.z, minVoxChunk.z);
                glm::uint zEnd = glm::min(triGrp->voxelBBox.maxVox.z, maxVoxChunk.z);
                for(glm::uint threadZ = zStart;
                    threadZ <= zEnd;
                    threadZ += threadsPerBlock.z)
                {
                    dim3 curThreadsPerBlock = threadsPerBlock;
                    if(threadZ + curThreadsPerBlock.z > zEnd)
                        curThreadsPerBlock.z = zEnd - threadZ + 1;
                    ComputeVoxelization<<<numBlocks, curThreadsPerBlock>>>(_pgVerts, triOffset, 
                                                                        _pgEdges, 
                                                                        _pgFaceNormals,
                                                                        _pgBounds, 
                                                                        _offsetP, _deltaP, 
                                                                        minVoxChunk, maxVoxChunk,
                                                                        threadZ,
                                                                        voxelTriCountsDevPtr,
                                                                        voxelTriIndicesDevPtr);
                    s_cudaStatus = cudaDeviceSynchronize();
                    if(s_cudaStatus != cudaSuccess)
                    {
                        _error << "generateVoxelsAndOctTree() error: " << s_cudaStatus << "." << std::endl;
                        return 0;
                    }
                }
            }
#ifdef __DEBUG__
            if(trisOverlapVoxChunk && 
                (s_debug_output || s_debug_svg == true))
            {
                if(s_debug_svg)
                {
                    std::cout << "Debugging svg, ";
                            
                    std::stringstream dbgFileName;
                    dbgFileName << outputDir << "/" << "VoxelizerDebug-" 
                                << minVoxChunk.x << "-"
                                << minVoxChunk.y << "-"
                                << minVoxChunk.z << "-"
                                << triGroupOffset;

                    size_t bufSize = voxelTriIndicesDevPtr.pitch 
                                    * voxChunkDim.y
                                    * voxChunkDim.z;
                    glm::lowp_ivec4* pTriIndicesBuf = (glm::lowp_ivec4*)malloc(bufSize);
                    if(pTriIndicesBuf != NULL)
                    {
                        cudaMemcpy(pTriIndicesBuf, voxelTriIndicesDevPtr.ptr, bufSize, cudaMemcpyDeviceToHost);
                     
                        ExportToSvg(voxChunkDim, voxelTriIndicesDevPtr.pitch,
                                    pTriIndicesBuf, 
                                    triGrp->pVerts, triGroupOffset, 
                                    voxOffsetOrigin,
                                    glm::vec3(1.0f / _deltaP.x,
                                                1.0f / _deltaP.y,
                                                1.0f / _deltaP.z),
                                    dbgFileName.str());

                        free(pTriIndicesBuf);
                    }
                    else
                    {
                        std::cout << "Failed to allocate svg debug memory for " 
                                    << dbgFileName.str() << "."
                                    << std::endl;
                    }
                }
                        
                if(s_debug_output == true)
                {
                    s_debug_voxels = true;
                    s_debug_scalars = true;
                }
            }
#endif

            if(triGrp->pImageData != nullptr && triGrp->pUVs != nullptr)
            { 
                ImageMap::iterator findIt = _imageMap.find(triGrp->pImageData);
                if(findIt == _imageMap.end())
                {
                    findIt = _imageMap.insert(std::make_pair(triGrp->pImageData,
                                                                nullptr)).first;
                }

                if(findIt->second == nullptr && trisOverlapVoxChunk)
                {
                    s_cudaStatus = cudaMallocArray(&findIt->second, 
                                                    &triGrp->imageDesc, 
                                                    triGrp->imageWidth,
                                                    triGrp->imageHeight);
                    if(s_cudaStatus != cudaSuccess)
                    {
                        _error << "generateVoxelsAndOctTree() failed to allocate image array, error: " << s_cudaStatus << "." << std::endl;
                        return 0;
                    }

                    s_totalAllocatedDeviceMemory += (sizeof(unsigned char) * 4 * triGrp->imageWidth * triGrp->imageHeight);

                    s_cudaStatus = cudaMemcpyToArray(findIt->second,
                                                        0u, 0u, 
                                                        triGrp->pImageData,
                                                        sizeof(unsigned char) * 4
                                                        * triGrp->imageWidth 
                                                        * triGrp->imageHeight,
                                                        cudaMemcpyHostToDevice);
                    if(s_cudaStatus != cudaSuccess)
                    {
                        _error << "generateVoxelsAndOctTree() failed to memcpy image data to image array, error: " << s_cudaStatus << "." << std::endl;
                        return 0;
                    }
                }
                        
                if(pCurImage != triGrp->pImageData && trisOverlapVoxChunk)
                {
                    pCurImage = triGrp->pImageData;
                    if(!BindTextureToArray(findIt->second,
                                            triGrp->imageDesc,
                                            triGrp->texAddressMode0,
                                            triGrp->texAddressMode1))
                    {
                        _error << "generateVoxelsAndOctTree() failed to bind texture to array, error: " << s_cudaStatus << "." << std::endl;
                        return 0;
                    }
                }
                        
                if(trisOverlapVoxChunk)
                {
                    ComputeColorsAndNormals<<<computeCNBlocksPerGrid, computeCNThreadsPerBlock>>>(triGroupOffset,
                                                                                        _pgVerts, 
                                                                                        perVtxNormals ? 
                                                                                            &_pgVtxNormals[normalsOffset] : nullptr,
                                                                                        _pgFaceNormals,
                                                                                        &_pgUVs[uvOffset],
                                                                                        triGrp->isTerrain,
                                                                                        voxOffsetOrigin,
                                                                                        _deltaP,
                                                                                        voxelTriCountsDevPtr,
                                                                                        voxelTriIndicesDevPtr,
                                                                                        voxelColorsDevMipMapPtr,
                                                                                        voxelNormalsDevMipMapPtr,
                                                                                        voxChunkDim);
                    //uvOffset += triGrp->pUVs->size();
                }
            }
            else if(trisOverlapVoxChunk)
            {
                ComputeColorsAsNormals<<<computeCNBlocksPerGrid, computeCNThreadsPerBlock>>>(triGroupOffset,
                                                                                        _pgVerts, 
                                                                                        perVtxNormals ? 
                                                                                            &_pgVtxNormals[normalsOffset] : nullptr,
                                                                                        _pgFaceNormals,
                                                                                        voxOffsetOrigin,
                                                                                        _deltaP,
                                                                                        voxelTriCountsDevPtr,
                                                                                        voxelTriIndicesDevPtr,
                                                                                        voxelColorsDevMipMapPtr,
                                                                                        voxelNormalsDevMipMapPtr,
                                                                                        voxChunkDim);
            }
                    
            if(trisOverlapVoxChunk)
            {
                s_cudaStatus = cudaDeviceSynchronize();
                if(s_cudaStatus != cudaSuccess)
                {
                    _error << "generateVoxelsAndOctTree() error: " << s_cudaStatus << "." << std::endl;
                    return 0;
                }
            }

            if(perVtxNormals)
                normalsOffset += triGrp->pVertNormals->size();

            if(triGrp->pUVs != nullptr)
                uvOffset += triGrp->pUVs->size();

            triGroupOffset += numTris;
        }
                
        //std::cout << "Computing mipmaps, ";
        // TODO if no data was voxelized then we can replace this with memset to zero
        if(!computeMipMaps(voxChunkDim.x,//host memory chunk dimensions 
                            voxChunkDim.y, 
                            voxChunkDim.z, 
                            voxChunkDim.x>>1,
                            voxChunkDim.y>>1,
                            voxChunkDim.z>>1,
                            _voxelDeviceMipMaps,
                            voxelColorMipMaps,
                            voxelNormalMipMaps))
        {
            return 0;
        }

        VoxelColors& voxelColors = voxelColorMipMaps.front().colors;
        VoxelNormals& voxelNormals = voxelNormalMipMaps.front().normals;

        //update headers, update compute oct-tree, write data in chunks
        copyDeviceChunkToHostMipMapChunk(voxelColors,
                                            voxelNormals,
                                            voxChunkDim.x,//host memory chunk dimensions 
                                            voxChunkDim.y, 
                                            voxChunkDim.z,
                                            voxelColorsDevMipMapPtr,
                                            voxelNormalsDevMipMapPtr);//device memory dimensions
#ifdef __DEBUG__
        if(s_debug_mipmaps)
        {
            std::cout << "Debugging mipmaps, ";
            
            for(size_t depth = 1u; depth < voxelColorMipMaps.size(); ++depth)
            {
                const VoxelColors& voxelMMColors = voxelColorMipMaps[depth].colors;
                VoxelNormals& voxelMMNormals = voxelNormalMipMaps[depth].normals;
                const glm::uvec3& dim = voxelColorMipMaps[depth].dim;
        
                std::stringstream dbgFileName;
                dbgFileName << outputDir << "/" << "VoxelizerMipMapDebug-"
                            << x << "-"
                            << y << "-"
                            << z << "-"
                            << dim.x << "-"
                            << dim.y << "-"
                            << dim.z << "-"
                            << depth
                            << ".voxb";

                glm::uvec3 rangeStart(0);
                glm::uvec3 rangeEnd(dim);
                std::vector<std::string> debugVoxelMMColorFiles;
                std::vector<glm::uvec3> debugVoxelMMColorStartRanges;
                std::vector<glm::uvec3> debugVoxelMMColorEndRanges;
                //ExportMipMapToSvg(voxelColors,
                //                  dim,
                //                  voxOffsetOrigin,
                //                  glm::vec3(1.0f / _deltaP.x,
                //                            1.0f / _deltaP.y,
                //                            1.0f / _deltaP.z),
                //                  dbgFileName.str());
                debugVoxelMMColorFiles.push_back(dbgFileName.str());
                debugVoxelMMColorStartRanges.push_back(rangeStart);
                debugVoxelMMColorEndRanges.push_back(rangeEnd);
                VoxelBrickWriter::ExportToFile(voxelMMColors, voxelNormals,
                                              false,
                                              dim.x,//host memory chunk dimensions 
                                              dim.y, 
                                              dim.z,
                                              0, 0, 0,
                                              dim.x, dim.y, dim.z,
                                              dbgFileName.str());
                std::stringstream debugFileHeader;
                debugFileHeader << dbgFileName.str() << ".voxt";
                VoxelBrickWriter::ExportToFileHeader(dim.x,
                                                     dim.y,
                                                     dim.z,
                                                     debugFileHeader.str(),
                                                     false,
                                                     debugVoxelMMColorFiles,
                                                     debugVoxelMMColorStartRanges,
                                                     debugVoxelMMColorEndRanges);
            }
        }
#endif
#ifdef __DEBUG__
        if(s_debug_voxels == true || s_debug_scalars == true)
        {
            glm::uvec3 voxChunkStart(_extraVoxChunk);
            glm::uvec3 voxChunkEnd(maxVoxChunk + glm::uvec3(1));
            glm::uvec3 rangeStart(0);
            if(minVoxChunk.x != 0)
            {
                voxChunkStart.x = 0;
                rangeStart.x = minVoxChunk.x - _extraVoxChunk.x;
                if(maxVoxChunk.x > _voxDim.x)
                    voxChunkEnd.x = _voxDim.x - rangeStart.x;
                else
                    voxChunkEnd.x = maxVoxChunk.x - minVoxChunk.x + 1u;
            }

            if(minVoxChunk.y != 0)
            {
                voxChunkStart.y = 0;
                rangeStart.y = minVoxChunk.y - _extraVoxChunk.y;
                if(maxVoxChunk.y > _voxDim.y)
                    voxChunkEnd.y = _voxDim.y - rangeStart.y;
                else
                    voxChunkEnd.y = maxVoxChunk.y - minVoxChunk.y + 1u;
            }

            if(minVoxChunk.z != 0)
            {
                voxChunkStart.z = 0;
                rangeStart.z = minVoxChunk.z - _extraVoxChunk.z;
                if(maxVoxChunk.z > _voxDim.z)
                    voxChunkEnd.z = _voxDim.z - rangeStart.z;
                else
                    voxChunkEnd.z = maxVoxChunk.z - minVoxChunk.z + 1u;
            }
            
            glm::uvec3 rangeEnd(rangeStart + (voxChunkEnd - voxChunkStart));
            if(s_debug_voxels)
            {
                std::cout << "Debugging voxels, ";
                                        
                std::stringstream dbgFileName;
                dbgFileName << outputDir << "/" << "VoxelizerDebug-" 
                            << rangeStart.x << "-"
                            << rangeStart.y << "-"
                            << rangeStart.z << ".voxb";
                debugVoxelColorFiles.push_back(dbgFileName.str());
                debugVoxelColorStartRanges.push_back(rangeStart);
                debugVoxelColorEndRanges.push_back(rangeEnd);
                VoxelBrickWriter::ExportToFile(voxelColors, voxelNormals,
                                                debugFormatIsUByte,
                                                voxChunkDim.x,//host memory chunk dimensions 
                                                voxChunkDim.y, 
                                                voxChunkDim.z,
                                                voxChunkStart.x, voxChunkStart.y, voxChunkStart.z,
                                                voxChunkEnd.x, voxChunkEnd.y, voxChunkEnd.z,
                                                dbgFileName.str());
                s_debug_voxels = false;
            }

            if(s_debug_scalars)
            {
                std::cout << "Debugging scalars, ";

                std::stringstream dbgFileName;
                dbgFileName << outputDir << "/" << "VoxelizerScalars-" 
                            << rangeStart.x << "-"
                            << rangeStart.y << "-"
                            << rangeStart.z << ".voxs";

                size_t bufSize = voxelTriCountsDevPtr.pitch 
                                * voxChunkDim.y
                                * voxChunkDim.z;
                glm::uint* pTriCountsBuf = (glm::uint*)malloc(bufSize);

                if(pTriCountsBuf != NULL)
                {
                    cudaMemcpy(pTriCountsBuf, voxelTriCountsDevPtr.ptr, bufSize, cudaMemcpyDeviceToHost);

                    ExportToScalarFile(voxScalarFile,
                                        voxChunkDim,
                                        voxelTriCountsDevPtr.pitch,
                                        voxChunkStart.x, voxChunkStart.y, voxChunkStart.z,
                                        voxChunkEnd.x, voxChunkEnd.y, voxChunkEnd.z,
                                        pTriCountsBuf,
                                        dbgFileName.str(),
                                        rangeStart,
                                        rangeEnd);

                    free(pTriCountsBuf);
                }
                else
                {
                    std::cout << "Failed to allocate debug scalars memory for " 
                                << dbgFileName.str() << "."
                                << std::endl;
                }

                s_debug_voxels = false;
                s_debug_scalars = false;
            }
        }
#endif
        //copy root brick chunk into full res root brick
        //CopyRootBrickChunkToRootBrick(rootBrickXOffset, 
        //                              rootBrickYOffset, 
        //                              rootBrickZOffset,
        //                              voxelColorMipMaps.back(),
        //                              voxelNormalMipMaps.back(),
        //                              rootBrickColors,
        //                              rootBrickNormals);
#ifdef __DEBUG_MIPMAPS__
        //PrintVoxelColors(voxelColorMipMaps.front(), _voxDim);
        if(!ValidateVoxelMipMaps(voxelColorMipMaps, voxelNormalMipMaps, voxChunkDim, _numMipMapLevels))
        {
            _error << "Failed to validate mip maps." << std::endl;
            return 0;
        }
#endif
        //std::cout << "Compute octree.";
                
        computeOctTreeNodes(x, y, z,
                            _voxelDeviceMipMaps,
                            voxelColorMipMaps,
                            voxelNormalMipMaps,
                            voxelBrickWriters,
                            voxChunkDim,
                            _extraVoxChunk,
                            octTreeDeviceBuffers,
                            octTreeNodes,
                            octTreeConstColors,
                            _octTreeNodesWriteIndex,
                            _numMipMapLevels-1);
                
        //std::cout << "Write stored bricks, ";

        WriteCompletedStoredBricks(voxelBrickWriters, octTreeNodes);

        std::cout << "Done." << std::endl;
    }
#ifdef __DEBUG__
    if(debugVoxelColorFiles.size() > 0)
    {
        std::stringstream debugFileHeader;
        debugFileHeader << outputDir << "/VoxelizerDebug.voxt";
        VoxelBrickWriter::ExportToFileHeader(_voxDim.x,
                                             _voxDim.y,
                                             _voxDim.z,
                                             debugFileHeader.str(),
                                             debugFormatIsUByte,
                                             debugVoxelColorFiles,
                                             debugVoxelColorStartRanges,
                                             debugVoxelColorEndRanges);
    }
#endif

    if(atLeastOneTriGroupOverlapsVoxChunk)
    {
        std::cout << "Collapsed constant branches, ";

        CollapseConstantBranches(octTreeNodes);

        std::cout << "Write octree, ";

        bool retVal = writeOctTree(outputDir, outputBinary, outputCompressed, voxelBrickWriters);

        std::cout << "Done." << std::endl;

        return 1;
    }

    return -1;
}

static bool WriteOctTreeNode(std::ofstream& treeFile,
                             std::stringstream& error,
                             VoxelBrickWriters& brickWriters,
                             const Voxelizer::OctTreeNodes& octTreeNodes,
                             const Voxelizer::OctTreeConstColors& octTreeConstColors,
                             size_t xOffset,//offset in mip map
                             size_t yOffset,
                             size_t zOffset,
                             size_t xSize,//size of mip map
                             size_t ySize,
                             size_t zSize,
                             const glm::uvec3& brickDim,
                             size_t octTreeDepth,
                             size_t curX=0,
                             size_t curY=0,
                             size_t curZ=0,
                             size_t curXSize=1,
                             size_t curYSize=1,
                             size_t curZSize=1,
                             size_t curStart=0,
                             size_t childX=0,
                             size_t childY=0,
                             size_t childZ=0,
                             size_t childXSize=2,
                             size_t childYSize=2,
                             size_t childZSize=2,
                             size_t nextStartIndex=1)
{
    //size_t maxOctTreeDepth = voxelColorMipMaps.size()-1;
    
    //const VoxelColors& voxelColors = voxelColorMipMaps.at(voxelColorMipMaps.size() - 1 - octTreeDepth).colors;
    //const VoxelNormals& voxelNormals = voxelNormalMipMaps.at(voxelNormalMipMaps.size() - 1 - octTreeDepth).normals;

    size_t childStartIndex = nextStartIndex + 
                             ((childZ * childYSize * childXSize) + (childY * childXSize) + childX);
    size_t curIndex = curStart + 
                      ((curZ * curYSize * curXSize) + (curY * curXSize) + curX);

    const Voxelizer::OctTreeNode& curOctTreeNode = octTreeNodes[curIndex];
    
    for(size_t i = 0; i <= octTreeDepth; ++i)
        treeFile << "    ";//indent

    if(curOctTreeNode == 1u)//non-const
    {
        VoxelBrickWriter& brickWriter = brickWriters.at(octTreeDepth);
        treeFile << "<Node "
                 << "MipMapX=\"" << xOffset << "\" "
                 << "MipMapY=\"" << yOffset << "\" "
                 << "MipMapZ=\"" << zOffset << "\" "
                 << "Type=\"NON-CONST\" "
                 << "Brick=\"" << brickWriter.getBrickIndex(curX, curY, curZ) << "\" "
                 << "Depth=\"" << octTreeDepth << "\"";
        
        /*bool isHighestResBrick = octTreeDepth == maxOctTreeDepth;

        if(!brickWriter.writeBrick(xOffset, yOffset, zOffset,
                               xSize, ySize, zSize,
                               brickDim,
                               isHighestResBrick,
                               voxelColors, voxelNormals))
        {
            error << "Failed to write brick " 
                  << (brickWriter.getBrickCount()-1) 
                  << " to file: " 
                  << brickWriter.getOutputFileName() << "." << std::endl;
            return false;
        }*/
    }
    else//const node
    {
        //size_t firstVoxelInBrick = (zOffset * ySize * xSize) + (yOffset * xSize) + xOffset;
        //node type constant-color
        treeFile << "<Node "
                 << "MipMapX=\"" << xOffset << "\" "
                 << "MipMapY=\"" << yOffset << "\" "
                 << "MipMapZ=\"" << zOffset << "\" "
                 << "Type=\""
                 << (curOctTreeNode == 0 ? "CONST" : "LEAF-CONST")//LEAF-CONST means that I am const and all my children are too
                 << "\" "
                 << "ColorR=\"" 
                 << (sizeof(cuda::VoxColor) == 4 ?
                        static_cast<float>(octTreeConstColors.at(curIndex).x) / 255.0 :
                        octTreeConstColors.at(curIndex).x)
                 << "\" "
                 << "ColorG=\"" 
                 << (sizeof(cuda::VoxColor) == 4 ?
                        static_cast<float>(octTreeConstColors.at(curIndex).y) / 255.0 :
                        octTreeConstColors.at(curIndex).y)
                 << "\" "
                 << "ColorB=\""
                 << (sizeof(cuda::VoxColor) == 4 ?
                        static_cast<float>(octTreeConstColors.at(curIndex).z) / 255.0 :
                        octTreeConstColors.at(curIndex).z)
                 << "\" "
                 << "ColorA=\""
                 << (sizeof(cuda::VoxColor) == 4 ?
                        static_cast<float>(octTreeConstColors.at(curIndex).w) / 255.0 :
                        octTreeConstColors.at(curIndex).w)
                 << "\" "
                 << "Depth=\"" 
                 << octTreeDepth << "\"";
    }

    if(childStartIndex < octTreeNodes.size() //if we are not at leaf node
#ifndef __DEBUG__  
        && curOctTreeNode != 2//if current node is 2 then all our children are constant so we can skip them
#endif
      )
    {
        //end <Node> tag
        treeFile << ">"
                 << std::endl;

        size_t childMipMapXSize = xSize << 1;//child mip map size
        size_t childMipMapYSize = ySize << 1; 
        size_t childMipMapZSize = zSize << 1;

        size_t gChildXSize = childXSize << 1;
        size_t gChildYSize = childYSize << 1;
        size_t gChildZSize = childZSize << 1;
        size_t gChildStartIndex = nextStartIndex + (childXSize * childYSize * childZSize);

        size_t endX = childX + 2;
        size_t endY = childY + 2;
        size_t endZ = childZ + 2;
        for(size_t zChild = childZ, childZOffset = (zOffset << 1); 
            zChild < endZ; 
            ++zChild, childZOffset += brickDim.z)
        {
            size_t gChildZ = zChild << 1;
            for(size_t yChild = childY, childYOffset = (yOffset << 1);
                yChild < endY; 
                ++yChild, childYOffset += brickDim.y)
            {
                size_t gChildY = yChild << 1;
                for(size_t xChild = childX, childXOffset = (xOffset << 1);
                    xChild < endX;
                    ++xChild, childXOffset += brickDim.x)
                {
                    size_t gChildX = xChild << 1;
#ifdef __DEBUG__
                    if(curOctTreeNode == 2)
                    {
                        size_t childIndex = nextStartIndex + 
                                ((zChild * childYSize * childXSize) + (yChild * childXSize) + xChild);
                        //validate that all my children are constant
                        const Voxelizer::OctTreeNode& childOctTreeNode = octTreeNodes[childIndex];
                        if(childOctTreeNode == 1)
                        {
                            error << "Collapsed constant node is not valid." << std::endl;
                            return false;
                        }
                    }
#endif
                    if(!WriteOctTreeNode(treeFile, 
                                         error, 
                                         brickWriters, 
                                         //voxelColorMipMaps, 
                                         //voxelNormalMipMaps, 
                                         octTreeNodes,
                                         octTreeConstColors,
                                         childXOffset, childYOffset, childZOffset,
                                         childMipMapXSize, childMipMapYSize, childMipMapZSize,
                                         brickDim,
                                         octTreeDepth + 1, 
                                         xChild, yChild, zChild, 
                                         childXSize, childYSize, childZSize, 
                                         nextStartIndex, 
                                         gChildX, gChildY, gChildZ,
                                         gChildXSize, gChildYSize, gChildZSize,
                                         gChildStartIndex))
                    {
                        return false;
                    }
                }
            }
        }

        for(size_t i = 0; i <= octTreeDepth; ++i)
            treeFile << "    ";//indent
        treeFile << "</Node>" << std::endl;
    }
    else
    {
        //end <Node> tag
        treeFile << " />"
                 << std::endl;
    }

    return true;
}

bool Voxelizer::writeOctTree(const std::string& outputDir, 
                             bool outputBinary,
                             bool outputCompressed,
                             VoxelBrickWriters& voxelBrickWriters)

{
    std::string outFileName = outputDir + "/tree.gvx";
    std::ofstream treeFile(outFileName, std::fstream::out);
    if(!treeFile.is_open())
    {
        _error << "Failed to open " << outFileName << " for output." << std::endl;
        return false;
    }

    treeFile << "<GigaVoxelsOctTree "
             << "MaxDepth=\"" << _voxelColorMipMaps.size() << "\" "
             << "X=\"" << _p.x << "\" "
             << "Y=\"" << _p.y << "\" "
             << "Z=\"" << _p.z << "\" "
             << "DeltaX=\"" << _deltaP.x << "\" "
             << "DeltaY=\"" << _deltaP.y << "\" "
             << "DeltaZ=\"" << _deltaP.z << "\" "
             << "VolumeXSize=\"" << _voxDim.x << "\" "
             << "VolumeYSize=\"" << _voxDim.y << "\" "
             << "VolumeZSize=\"" << _voxDim.z << "\" "
             << "BrickXSize=\"" << _brickDim.x << "\" "
             << "BrickYSize=\"" << _brickDim.y << "\" "
             << "BrickZSize=\"" << _brickDim.z << "\" "
             << "Binary=\"" << (outputBinary ? "YES" : "NO") << "\" "
             << "Compressed=\"" << (outputCompressed ? "YES" : "NO") << "\" >"
             << std::endl;

    size_t xOffset = 0;
    size_t yOffset = 0;
    size_t zOffset = 0;
    size_t xSize = _brickDim.x;
    size_t ySize = _brickDim.y;
    size_t zSize = _brickDim.z;
    size_t octTreeDepth = 0;

    bool retVal = WriteOctTreeNode(treeFile, _error, 
                                   voxelBrickWriters, 
                                   _octTreeNodes,
                                   _octTreeConstColors,
                                   xOffset, yOffset, zOffset,
                                   xSize, ySize, zSize,
                                   _brickDim, octTreeDepth);

    treeFile << "</GigaVoxelsOctTree>"
             << std::endl;

    return retVal;
}

void Voxelizer::freeVoxelDeviceMipMaps()
{
    for(VoxelDeviceMipMaps::iterator itr = _voxelDeviceMipMaps.begin();
        itr != _voxelDeviceMipMaps.end();
        ++itr)
    {
        cudaFree(itr->first.ptr);
        cudaFree(itr->second.ptr);
    }

    _voxelDeviceMipMaps.clear();
}

void Voxelizer::freeVoxelMipMapsAndOctTree()
{
    VoxelColorMipMaps empty1;
    _voxelColorMipMaps.swap(empty1);

    VoxelNormalMipMaps empty2;
    _voxelNormalMipMaps.swap(empty2);

    OctTreeNodes empty3;
    _octTreeNodes.swap(empty3);

    OctTreeConstColors empty4;
    _octTreeConstColors.swap(empty4);
}

bool Voxelizer::copyDeviceChunkToHostMipMapChunk(VoxelColors& voxelColors,
                                                 VoxelNormals& voxelNormals,
                                                 size_t voxDimX, size_t voxDimY, size_t voxDimZ,
                                                 const cudaPitchedPtr& voxelColorsDevPtr,
                                                 const cudaPitchedPtr& voxelNormalsDevPtr)
{
    cuda::VoxColor* pVoxelChunkColors = NULL;
    size_t sizeOfColorsRow = sizeof(cuda::VoxColor) * voxDimX;
    if(sizeOfColorsRow == voxelColorsDevPtr.pitch)
    {
        pVoxelChunkColors = &voxelColors[0];
    }
    else
    {
        pVoxelChunkColors = (cuda::VoxColor*)malloc(voxDimZ *  voxelColorsDevPtr.ysize * voxelColorsDevPtr.pitch);
    }

    s_cudaStatus = cudaMemcpy(pVoxelChunkColors,
                              voxelColorsDevPtr.ptr, 
                              voxDimZ *  voxelColorsDevPtr.ysize * voxelColorsDevPtr.pitch, 
                              cudaMemcpyDeviceToHost);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "generateVoxelsAndOctTree() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }

    cuda::VoxNorm* pVoxelChunkNormals = NULL;
    size_t sizeOfNormalsRow = sizeof(cuda::VoxNorm) * voxDimX;
    if(sizeOfNormalsRow == voxelNormalsDevPtr.pitch)
    {
        pVoxelChunkNormals = &voxelNormals[0];
    }
    else
    {
        pVoxelChunkNormals = (cuda::VoxNorm*)malloc(voxDimZ * voxelNormalsDevPtr.ysize * voxelNormalsDevPtr.pitch);
    }
                        
    s_cudaStatus = cudaMemcpy(pVoxelChunkNormals,
                              voxelNormalsDevPtr.ptr,  
                              voxDimZ * voxelNormalsDevPtr.ysize * voxelNormalsDevPtr.pitch, 
                              cudaMemcpyDeviceToHost);
    if (s_cudaStatus != cudaSuccess)
    {
        _error << "generateVoxelsAndOctTree() error: " << s_cudaStatus << "." << std::endl;
        return false;
    }

    if(sizeOfColorsRow == voxelColorsDevPtr.pitch && sizeOfNormalsRow == voxelNormalsDevPtr.pitch)
        return true;

    char* colorReadPtr = (char*)pVoxelChunkColors;
    size_t colorPitch = voxelColorsDevPtr.pitch;
    size_t colorSlicePitch = colorPitch * voxelColorsDevPtr.ysize;

    char* normalsReadPtr = (char*)pVoxelChunkNormals;
    size_t normalsPitch = voxelNormalsDevPtr.pitch;
    size_t normalsSlicePitch = normalsPitch * voxelNormalsDevPtr.ysize;
    
    for(size_t z = 0; z < voxDimZ; ++z)
    {
        char* colorSlice = colorReadPtr + z * colorSlicePitch;
        char* normalsSlice = normalsReadPtr + z * normalsSlicePitch;
        for(size_t y = 0; y < voxDimY; ++y)
        {
            cuda::VoxColor* readColorRowPtr = (cuda::VoxColor*)(colorSlice + (y * colorPitch));
            cuda::VoxNorm* readNormalsRowPtr = (cuda::VoxNorm*)(normalsSlice + (y * normalsPitch));

            size_t voxIndex = (z * voxDimX * voxDimY) + (y * voxDimX);
    
            if(sizeOfColorsRow != voxelColorsDevPtr.pitch)
                memcpy(&voxelColors[voxIndex], readColorRowPtr, sizeOfColorsRow);
            if(sizeOfNormalsRow != voxelNormalsDevPtr.pitch)
                memcpy(&voxelNormals[voxIndex], readNormalsRowPtr, sizeOfNormalsRow);
        }
    }

    if(sizeOfColorsRow != voxelColorsDevPtr.pitch)
        free(pVoxelChunkColors);
    if(sizeOfNormalsRow != voxelNormalsDevPtr.pitch)
        free(pVoxelChunkNormals);

    return true;
}

const std::string& Voxelizer::getErrorMessage()
{
    _error << " Cuda Error Code: " 
           << cudaPeekAtLastError() 
           << " '" 
           << cudaGetErrorString(cudaGetLastError()) 
           << "'" 
           << std::endl;
    static std::string errorMsg;
    errorMsg = _error.str();
    return errorMsg;
}

//copy voxel colors to cpu buffer
                //this is only a portion of the full 3D
                //s_cudaStatus = cudaMemcpy(&voxelChunkTriCounts.front(), 
                //                            voxelTriCountsDevPtr.ptr,  
                //                            voxExt.depth * voxExt.height * voxelTriCountsDevPtr.pitch, 
                //                            cudaMemcpyDeviceToHost);
                //if (s_cudaStatus != cudaSuccess)
                //{
                //    _error << "generateVoxelsAndOctTree() error: " << s_cudaStatus << "." << std::endl;
                //    return false;
                //}
                //debugging code
                //size_t voxIndex = (z * _voxDim.x * _voxDim.y) + (y * _voxDim.x) + x;

                //char* triCountsReadPtr = (char*)&voxelChunkTriCounts.front();
                //size_t triCountsPitch = voxelTriCountsDevPtr.pitch;
                //size_t triCountsSlicePitch = triCountsPitch * voxExt.height;
                //for(size_t zIndex = 0; zIndex < zSize; ++zIndex)
                //{
                //    char* triCountsSlice = triCountsReadPtr + zIndex * triCountsSlicePitch;
                //    for(size_t yIndex = 0; yIndex < ySize; ++yIndex)
                //    {
                //        glm::uint* readTriCountsRowPtr = (glm::uint*)(triCountsSlice + yIndex * triCountsPitch);
                //        for(size_t voxIndexItr = voxIndex; voxIndexItr < voxIndex + xSize; ++voxIndexItr)
                //        {
                //            if(readTriCountsRowPtr[voxIndexItr-voxIndex] > 0)
                //            {
                //                std::cout << "[" << (voxIndexItr - voxIndex) << "][" << yIndex << "][" << zIndex << "]=(" 
                //                        << voxelColors[voxIndexItr].x << ", "
                //                        << voxelColors[voxIndexItr].y << ", "
                //                        << voxelColors[voxIndexItr].z << ", "
                //                        << voxelColors[voxIndexItr].w << ") ("
                //                        << voxelNormals[voxIndexItr].x << ", "
                //                        << voxelNormals[voxIndexItr].y << ", "
                //                        << voxelNormals[voxIndexItr].z << ") " 
                //                        << readTriCountsRowPtr[voxIndexItr-voxIndex]
                //                        << std::endl;
                //            }
                //            else if(voxelColors[voxIndexItr].w > 0.0f)
                //                std::cout << "Huh?";
                //        }
                //        voxIndex += _voxDim.x;//advance to next row
                //    }
                //}
