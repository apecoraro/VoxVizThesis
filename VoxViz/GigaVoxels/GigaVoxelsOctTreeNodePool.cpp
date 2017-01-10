#include "GigaVoxels/GigaVoxelsOctTreeNodePool.h"

#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "GigaVoxels/GigaVoxelsBrickPool.h"

#include "VoxVizOpenGL/GLUtils.h"

#include <algorithm>
#include <iostream>
#include <cmath>

using namespace gv;

OctTreeNodePool::OctTreeNodePool() :
    m_dimX(0),
    m_dimY(0),
    m_dimZ(0),
    //m_sizeOfNodePoolTexture(0),
    m_textureID(0),
    m_pboUploadID(0)
{
}

OctTreeNodePool::~OctTreeNodePool()
{
    for(NodePool::iterator itr = m_nodePool.begin();
        itr != m_nodePool.end();
        ++itr)
    {
        if(itr->get()->referenceCount() == 1
            && itr->get()->getBrickIsOnGpuFlag())
        {
            BrickPool::instance().notifyDeleted(itr->get());
        }
    }
    
    m_nodeTexturePointers.clear();

    m_nodePool.clear();

    m_nodeUpdateList.clear();

    if(m_textureID)
        glDeleteTextures(1, &m_textureID);
    if(m_pboUploadID)
        glDeleteBuffers(1, &m_pboUploadID);
}

void OctTreeNodePool::allocateChildNodeBlock(GigaVoxelsOctTree::Node* pParent, 
                                             size_t& blockIndex)
{
    //return index of block
    blockIndex = m_nodePool.size();
    if(pParent)
        pParent->setChildNodeBlockIndex(blockIndex);

    GigaVoxelsOctTree::NodeTexturePointer* pTexPtr = new GigaVoxelsOctTree::NodeTexturePointer();
    m_nodeTexturePointers.push_back(pTexPtr);
    unsigned char* pChildNodeBlock = pTexPtr->data();

    for(size_t i = 0; i < 8; ++i)
    {
        GigaVoxelsOctTree::Node* pNewNode = new GigaVoxelsOctTree::Node();
        
        m_nodePool.push_back(pNewNode);

        pNewNode->setDataPtr(pChildNodeBlock);
        pNewNode->setTexturePtr(pTexPtr);
        pNewNode->setParent(pParent);
        pNewNode->setNodePool(this);

        pChildNodeBlock += 8;//8 bytes (64 bits) per node
    }
}

GigaVoxelsOctTree::Node* OctTreeNodePool::getChild(size_t childIndex)
{
    if(m_nodePool.size() <= childIndex)
        return NULL;
    return m_nodePool[childIndex].get();
}

void OctTreeNodePool::get3DTextureDimensions(size_t& x, size_t& y, size_t& z) const
{
    x = m_dimX;
    y = m_dimY;
    z = m_dimZ;
}

void OctTreeNodePool::create3DTexture()
{
    if(!glewIsSupported("GL_EXT_texture_integer"))
        std::cerr << "ERROR: OctTreeNodePool requires support for GL_EXT_texture_integer." << std::endl;    

    if(!glewIsSupported("GL_ARB_texture_rg"))
        std::cerr << "ERROR: OctTreeNodePool requires support for GL_ARB_texture_rg." << std::endl;

    size_t numNodes = m_nodePool.size();
    if(numNodes == 0)
        return;

    size_t cubeRoot = static_cast<size_t>(std::pow(static_cast<double>(numNodes), 1.0 / 3.0));

    if(cubeRoot % 2 != 0)
    {
        --cubeRoot;//need dimensions to be a multiple of 2

        if(cubeRoot == 0)//numNodes is 1
            cubeRoot = 2;
    }

    m_dimX = m_dimY = m_dimZ = cubeRoot;
    while(m_dimX * m_dimY * m_dimZ < numNodes)
    {
        m_dimX += 2;
    }

    size_t textureSize = m_dimX * m_dimY * m_dimZ * sizeof(GigaVoxelsOctTree::Node::GpuDataStruct);

    unsigned char* pZero = new unsigned char[textureSize];
    memset(pZero, 0, textureSize);

    bool genMipMaps = false;
    bool useInterpolation = false;
    m_textureID = voxOpenGL::GLUtils::Create3DTexture(GL_RG32UI, 
                                                      m_dimX, 
                                                      m_dimY, 
                                                      m_dimZ, 
                                                      GL_RG_INTEGER, 
                                                      GL_UNSIGNED_INT, 
                                                      pZero, 
                                                      genMipMaps,
                                                      useInterpolation);

    m_pboUploadID = voxOpenGL::GLUtils::CreatePixelBufferObject(GL_PIXEL_UNPACK_BUFFER, 
                                                                textureSize, 
                                                                pZero,
                                                                GL_STREAM_DRAW);

    delete [] pZero;
    
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboUploadID);

    GLint blockDimX = m_dimX >> 1;
    GLint blockDimY = m_dimY >> 1;
    GLintptr writeOffset = 0;
    GLsizeiptr dataSize = 8 * sizeof(GigaVoxelsOctTree::Node::GpuDataStruct);
    for(size_t nodeTexIdx = 0;
        nodeTexIdx < m_nodeTexturePointers.size();
        ++nodeTexIdx,
        writeOffset += (sizeof(GigaVoxelsOctTree::Node::GpuDataStruct) * 8))
    {
        GigaVoxelsOctTree::Node::GpuDataStruct* 
            pNodeTextureData = 
                reinterpret_cast<GigaVoxelsOctTree::Node::GpuDataStruct*>(m_nodeTexturePointers.at(nodeTexIdx)->data());

        GLint z = nodeTexIdx / blockDimY / blockDimX;
        GLint y = (nodeTexIdx - (z * blockDimY * blockDimX)) / blockDimX;
        GLint x = (nodeTexIdx - (z * blockDimY * blockDimX) - (y * blockDimX));

        x <<= 1;
        y <<= 1;
        z <<= 1;

        size_t blockIndex = nodeTexIdx << 3;

        //initialize the texture data pointer for each node
        //and the 3d texture indices
        static const int xIncr[8] = { 1, -1, 1, -1, 1, -1, 1, 0 };
        static const int yIncr[8] = { 0,  1, 0, -1, 0,  1, 0, 0 };
        static const int zIncr[8] = { 0,  0, 0,  1, 0,  0, 0, 0 };
        size_t incrIndex = 0;
        size_t texX = x;
        size_t texY = y;
        size_t texZ = z;
        for(size_t i = blockIndex; i < blockIndex + 8; ++i, ++incrIndex)
        {
            GigaVoxelsOctTree::Node& node = *(m_nodePool[i].get());

            node.set3DTexturePtr(texX, texY, texZ);

            texX += xIncr[incrIndex];
            texY += yIncr[incrIndex];
            texZ += zIncr[incrIndex];

            size_t childBlockIndex = node.getChildNodeBlockIndex();
            if(childBlockIndex != 0)
            {
                childBlockIndex >>= 3;
                size_t childZ = childBlockIndex / blockDimY / blockDimX;
                size_t childY = 
                    (childBlockIndex - (childZ * blockDimY * blockDimX)) / blockDimX;
                size_t childX = 
                    (childBlockIndex - (childZ * blockDimY * blockDimX) - (childY * blockDimX));

                childX <<= 1;
                childY <<= 1;
                childZ <<= 1;
                node.setChildNodesPtr(childX, childY, childZ);
            }
        }

        void* pWritePtr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                           writeOffset, dataSize,
                                           GL_MAP_WRITE_BIT |
                                           GL_MAP_INVALIDATE_RANGE_BIT |
                                           GL_MAP_UNSYNCHRONIZED_BIT);

        memcpy(pWritePtr, pNodeTextureData, dataSize);

        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);

        voxOpenGL::GLUtils::Upload3DTexture(m_textureID,
                                            0,
                                            x, y, z,
                                            2, 2, 2,
                                            GL_RG_INTEGER,
                                            GL_UNSIGNED_INT,
                                            (const GLvoid*)writeOffset);
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void OctTreeNodePool::addToUpdateList(GigaVoxelsOctTree::Node* pNode)
{
    m_nodeUpdateList.push_back(pNode);
}

void OctTreeNodePool::update()
{
    if(m_nodeUpdateList.size() == 0)
        return;

    voxOpenGL::GLUtils::CheckOpenGLError();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pboUploadID);

    GLintptr writeOffset = 0;
    GLsizeiptr dataSize = sizeof(GigaVoxelsOctTree::Node::GpuDataStruct);
    for(NodeUpdateList::iterator itr = m_nodeUpdateList.begin();
        itr != m_nodeUpdateList.end();
        ++itr)
    {
        update3DTexture((*itr).get(), writeOffset, dataSize);
        writeOffset += dataSize;
    }

    m_nodeUpdateList.clear();

    voxOpenGL::GLUtils::CheckOpenGLError();

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

void OctTreeNodePool::update3DTexture(GigaVoxelsOctTree::Node* pNode, 
                                      size_t writeOffset, 
                                      size_t dataSize)
{
    size_t x, y, z;
    pNode->get3DTexturePtr(x, y, z);

    voxOpenGL::GLUtils::CheckOpenGLError();

    void* pWritePtr = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER,
                                       writeOffset, dataSize,
                                       GL_MAP_WRITE_BIT |
                                       GL_MAP_INVALIDATE_RANGE_BIT |
                                       GL_MAP_UNSYNCHRONIZED_BIT);
    
    voxOpenGL::GLUtils::CheckOpenGLError();

    GigaVoxelsOctTree::Node::GpuDataStruct* 
            pNodeTextureData = pNode->getDataPtr();

    memcpy(pWritePtr, pNodeTextureData, dataSize);

    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    
    //TODO optimize by coalescing texture updates?
    voxOpenGL::GLUtils::Upload3DTexture(m_textureID,
                                        0,
                                        x, y, z,
                                        1, 1, 1,
                                        GL_RG_INTEGER,
                                        GL_UNSIGNED_INT,
                                        (const GLvoid*)writeOffset);
}

unsigned int OctTreeNodePool::getTextureID() const
{
    return m_textureID;
}

size_t OctTreeNodePool::getNodeCount() const
{
    return m_nodePool.size();
}