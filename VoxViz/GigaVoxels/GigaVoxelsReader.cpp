#include "GigaVoxels/GigaVoxelsReader.h"
#include "GigaVoxels/GigaVoxelsSceneGraph.h"
#include "GigaVoxels/GigaVoxelsOctTree.h"
#include "GigaVoxels/GigaVoxelsOctTreeNodePool.h"

#include "VoxVizCore/SmartPtr.h"

#include "VoxVizCore/DataSetReader.h"

#include <QtCore/qxmlstream.h>
#include <QtCore/QFile>
#include <QtGui/QVector3D>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace gv;

bool GigaVoxelsReader::IsGigaVoxelsFile(const std::string& filename)
{
    std::string ext = vox::DataSetReader::GetFileExtension(filename);
    if(ext == "gvp" || ext == "gvx")
        return true;
    else
        return false;
}

static bool s_loadNormals = true;

void GigaVoxelsReader::SetLoadNormals(bool flag)
{
    s_loadNormals = flag;
}

static Group* LoadGVP(QXmlStreamReader& xmlStream, 
                      const std::string& filename)
{
    std::string filePath = vox::DataSetReader::GetFilePath(filename);

    vox::SmartPtr<gv::Group> spRoot = nullptr;
    vox::BoundingSphere rootSphere;
    bool isCompressed = false;
    int brickDimX;
    int brickDimY;
    int brickDimZ;
    while(xmlStream.readNextStartElement())
    {
        if(xmlStream.name() == "OctTrees")
        {
            spRoot = new gv::Group();
            isCompressed = (xmlStream.attributes().value("Compressed").toString().compare("YES") == 0);
            brickDimX = xmlStream.attributes().value("BrickXSize").toString().toInt();
            brickDimY = xmlStream.attributes().value("BrickYSize").toString().toInt();
            brickDimZ = xmlStream.attributes().value("BrickZSize").toString().toInt();
        }
        else if(spRoot.get() != nullptr &&
                xmlStream.name() == "OctTree")
        {
            QStringRef centerXStr = xmlStream.attributes().value("CenterX");
            QStringRef centerYStr = xmlStream.attributes().value("CenterY");
            QStringRef centerZStr = xmlStream.attributes().value("CenterZ");
            QVector3D center(centerXStr.toString().toFloat(),
                                centerYStr.toString().toFloat(),
                                centerZStr.toString().toFloat());
                
            float radius = xmlStream.attributes().value("Radius").toString().toFloat();

            rootSphere.expandBy(center, radius);

            QStringRef sizeMetersX = xmlStream.attributes().value("SizeXMeters");
            QStringRef sizeMetersY = xmlStream.attributes().value("SizeYMeters");
            QStringRef sizeMetersZ = xmlStream.attributes().value("SizeZMeters");
            QVector3D octTreeSizeMeters(sizeMetersX.toString().toFloat(),
                                        sizeMetersY.toString().toFloat(),
                                        sizeMetersZ.toString().toFloat());

            QString treeFile = xmlStream.attributes().value("TreeFile").toString();

            std::string fullPath = filePath;
            fullPath += "/";
            fullPath += std::string(treeFile.toAscii().data());
            PagedOctTreeNode* pPagedOctTree = new PagedOctTreeNode(isCompressed,
                                                                   brickDimX,
                                                                   brickDimY,
                                                                   brickDimZ,
                                                                   center, 
                                                                   radius,
                                                                   octTreeSizeMeters,
                                                                   fullPath);

            spRoot->addChild(pPagedOctTree);

            xmlStream.skipCurrentElement();
        }
    }

    if(spRoot.get() != nullptr)
        spRoot->setBoundingSphere(rootSphere.center(), rootSphere.radius());

    return spRoot.release();
}

typedef std::vector<GigaVoxelsOctTree::Node*> NonConstantNodes;
typedef std::vector<NonConstantNodes> NonConstantNodesTree;

static bool LoadBinaryBricks(const std::string& binaryFile,
                             NonConstantNodes& nonConstantNodes)
{
    std::fstream voxFile;
    voxFile.open(binaryFile, std::ios_base::in | std::ios_base::binary);
    if(!voxFile.is_open())
    {
        std::cerr << "ERROR: Failed to open " << binaryFile << std::endl;
        return false;
    }

    int compressed;
    voxFile.read((char*)&compressed, sizeof(int));

    //if(compressed == 0)
    //{
    //    std::cerr << "ERROR: Attempting to read compressed file " << binaryFile << ", but it is not compressed." << std::endl;
    //    return false;
    //}

    for(size_t i = 0;
        i < nonConstantNodes.size();
        ++i)
    {
        GigaVoxelsOctTree::Node* pNode = nonConstantNodes.at(i);

        if(pNode == nullptr)
            break;

        nonConstantNodes[i] = nullptr;

        size_t brickDimX;
        size_t brickDimY;
        size_t brickDimZ;
        voxFile.read((char*)&brickDimX, sizeof(brickDimX));
        voxFile.read((char*)&brickDimY, sizeof(brickDimY));
        voxFile.read((char*)&brickDimZ, sizeof(brickDimZ));

        unsigned int brickBorderX;
        unsigned int brickBorderY;
        unsigned int brickBorderZ;
        voxFile.read((char*)&brickBorderX, sizeof(brickBorderX));
        voxFile.read((char*)&brickBorderY, sizeof(brickBorderY));
        voxFile.read((char*)&brickBorderZ, sizeof(brickBorderZ));

        int colorsCompressed;
        voxFile.read((char*)&colorsCompressed, sizeof(colorsCompressed));
        if(colorsCompressed != compressed)
        {
            std::cerr << "ERROR: compression mismatch found in brick file (colors)." << std::endl;
            return false;
        }

        unsigned int dataSize;
        voxFile.read((char*)&dataSize, sizeof(dataSize));
        if(dataSize == 0)
        {
            std::cerr << "ERROR: size of color data is zero?" << std::endl;
            return false;
        }

        char* pVoxelColors = new char[dataSize];
        voxFile.read(pVoxelColors, dataSize);

        if(voxFile.gcount() != dataSize)
        {
            std::cerr << "ERROR: tried to read " << dataSize << " color bytes, but read " << voxFile.gcount() << std::endl;
            return false;
        }

        pNode->setBrickColorsPtr(colorsCompressed, dataSize, pVoxelColors);

        int gradsCompressed;
        voxFile.read((char*)&gradsCompressed, sizeof(gradsCompressed));
        if(gradsCompressed != compressed)
        {
            std::cerr << "ERROR: compression mismatch found in brick file (gradients)." << std::endl;
            return false;
        }

        voxFile.read((char*)&dataSize, sizeof(dataSize));
        if(dataSize == 0)
        {
            std::cerr << "ERROR: size of gradient data is zero?" << std::endl;
            return false;
        }

        if(s_loadNormals)
        {
            char* pVoxelGradients = new char[dataSize];
            voxFile.read(pVoxelGradients, dataSize);

            if(voxFile.gcount() != dataSize)
            {
                std::cerr << "ERROR: tried to read " << dataSize << " gradient bytes, but read " << voxFile.gcount() << std::endl;
                return false;
            }

            pNode->setBrickGradientsPtr(gradsCompressed, dataSize, pVoxelGradients);
        }
        else
        {
            voxFile.seekg(dataSize, std::ios_base::cur);
        }

        pNode->setBrickData(brickDimX,
                            brickDimY,
                            brickDimZ,
                            brickBorderX,
                            brickBorderY,
                            brickBorderZ);
    }

    int checkEnd = voxFile.peek();
    return checkEnd == std::char_traits<char>::eof();
}

static void AddVertex(vox::FloatArray& vertexArray,
                      double x, double y, double z)
{
    vertexArray.push_back(static_cast<float>(x));
    vertexArray.push_back(static_cast<float>(y));
    vertexArray.push_back(static_cast<float>(z));
}

void CreateVertices(vox::SceneObject* pSceneObject)
{
    vox::FloatArray& vertexArray = pSceneObject->getFloatVertexArray();
    
    const vox::BoundingBox& bbox = pSceneObject->getBoundingBox();

    ////front
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());

    ////back
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());

    ////left
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());

    ////right
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());

    //top
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMax(), bbox.zMin());

    //bottom
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMin());
    AddVertex(vertexArray, bbox.xMax(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMax());
    AddVertex(vertexArray, bbox.xMin(), bbox.yMin(), bbox.zMin());

    pSceneObject->setPrimitiveType(GL_QUADS);//TODO switch to GL_TRIANGLES
    //pSceneObject->createVAO();
}

bool LoadOctTreeNode(QXmlStreamReader& xmlStream,
                    const std::string& filePath,
                    NonConstantNodesTree& nonConstantNodesTree,
                    GigaVoxelsOctTree* pOctTree, 
                    size_t curNodeIndex, 
                    size_t maxDepth, 
                    size_t octTreeDepth=0)
{
    GigaVoxelsOctTree::Node* pNode = pOctTree->getNodePool()->getChild(curNodeIndex);
    std::string treeDepthString = xmlStream.attributes().value("Depth").toString().toAscii();
    int curTreeDepth = xmlStream.attributes().value("Depth").toString().toInt();
    //sanity check
    int mipMapX = xmlStream.attributes().value("MipMapX").toString().toInt();
    int mipMapY = xmlStream.attributes().value("MipMapY").toString().toInt();
    int mipMapZ = xmlStream.attributes().value("MipMapZ").toString().toInt();
    if(curTreeDepth != octTreeDepth)
    {
        std::cerr << "ERROR: LoadOctTreeNode xml Depth attribute does not match octTreeDepth." << std::endl;
        return false;
    }

    pNode->setMipMapXYZDepth(mipMapX, mipMapY, mipMapZ, curTreeDepth);

    std::string typeStr = xmlStream.attributes().value("Type").toString().toAscii();
    if(typeStr == "CONST" ||
       typeStr == "LEAF-CONST")
    {
        pNode->setNodeTypeFlag(GigaVoxelsOctTree::Node::CONSTANT_NODE);
                    
        float red = xmlStream.attributes().value("ColorR").toString().toFloat();
        float green = xmlStream.attributes().value("ColorG").toString().toFloat();
        float blue = xmlStream.attributes().value("ColorB").toString().toFloat();
        float alpha = xmlStream.attributes().value("ColorA").toString().toFloat();

        pNode->setConstantValue(red, green, blue, alpha);
    }
    else
    {
        pNode->setNodeTypeFlag(GigaVoxelsOctTree::Node::NON_CONSTANT_NODE);
        int brickIndex = xmlStream.attributes().value("Brick").toString().toInt();

        NonConstantNodes& nonConstantNodes = nonConstantNodesTree.at(octTreeDepth);
        if(brickIndex >= (int)nonConstantNodes.size())
            nonConstantNodes.resize(brickIndex+1, nullptr);
        nonConstantNodes[brickIndex] = pNode;
    }

    bool retVal = true;

    if(octTreeDepth != maxDepth &&
       typeStr != "LEAF-CONST")
    {
        size_t childNodeStartIndex;
        pOctTree->getNodePool()->allocateChildNodeBlock(pNode, childNodeStartIndex);
        std::vector<GigaVoxelsOctTree::Node*> nonConstantNodes;

        for(size_t i = 0; i < 8; ++i)
        {
            bool parsedStartElement = false;
            while(!xmlStream.atEnd())
            {
                QXmlStreamReader::TokenType tokenType = xmlStream.readNext();
                parsedStartElement = (tokenType == QXmlStreamReader::StartElement);
                if(parsedStartElement)
                    break;
            }

            if(!parsedStartElement)
            {
                std::cerr << "ERROR: parsing next element failed." << std::endl;
                return false;
            }

            if(xmlStream.name() != "Node")
            {
                std::cerr << "ERROR: invalid node encounterred." << std::endl;
                return false;
            }

            if(!LoadOctTreeNode(xmlStream, filePath,
                                nonConstantNodesTree,
                                pOctTree, 
                                childNodeStartIndex + i,
                                maxDepth,
                                octTreeDepth + 1))
            {
                retVal = false;
                std::cerr << "ERROR in sub-LoadOctTreeNode." << std::endl;
            }
        }
    }
    else
        xmlStream.skipCurrentElement();

    return retVal;
}

static GigaVoxelsOctTree* LoadGVX(QXmlStreamReader& xmlStream,
                                  const std::string& filename)
{
    vox::SmartPtr<GigaVoxelsOctTree> spOctTree = nullptr;

    int maxTreeDepth = 0;
    //int mipMapSizeX = 0;
    //int mipMapSizeY = 0;
    //int mipMapSizeZ = 0;
    bool isBinary = false;
    bool isCompressed = false;
    bool gradientsAreUnsigned = true;
    bool fullyParsed = false;
    
    std::string filePath = vox::DataSetReader::GetFilePath(filename);

    while(xmlStream.readNextStartElement())
    {
        if(xmlStream.name() == "GigaVoxelsOctTree")
        {
            spOctTree = new GigaVoxelsOctTree();
            spOctTree->setFilename(filename);

            QVector3D min;
            min.setX(xmlStream.attributes().value("X").toString().toFloat());
            min.setY(xmlStream.attributes().value("Y").toString().toFloat());
            min.setZ(xmlStream.attributes().value("Z").toString().toFloat());
                
            QVector3D delta;
            delta.setX(xmlStream.attributes().value("DeltaX").toString().toFloat());
            delta.setY(xmlStream.attributes().value("DeltaY").toString().toFloat());
            delta.setZ(xmlStream.attributes().value("DeltaZ").toString().toFloat());

            size_t xSize = xmlStream.attributes().value("VolumeXSize").toString().toInt();
            size_t ySize = xmlStream.attributes().value("VolumeYSize").toString().toInt();
            size_t zSize = xmlStream.attributes().value("VolumeZSize").toString().toInt();

            size_t brickXSize = static_cast<size_t>(xmlStream.attributes().value("BrickXSize").toString().toInt());
            size_t brickYSize = static_cast<size_t>(xmlStream.attributes().value("BrickYSize").toString().toInt());
            size_t brickZSize = static_cast<size_t>(xmlStream.attributes().value("BrickZSize").toString().toInt());

            isBinary = (xmlStream.attributes().value("Binary").toString().compare("YES") == 0);
            isCompressed = (xmlStream.attributes().value("Compressed").toString().compare("YES") == 0);

            //hard code for now
            gradientsAreUnsigned = true;

            spOctTree->setBrickParams(brickXSize,
                                      brickYSize,
                                      brickZSize,
                                      isCompressed,
                                      gradientsAreUnsigned);

            QVector3D max(min.x() + (delta.x() * xSize),
                          min.y() + (delta.y() * ySize),
                          min.z() + (delta.z() * zSize));

            vox::BoundingBox bbox;
            bbox.set(min, max);

            float rayStepSizeX = delta.x() / (bbox.xMax() - bbox.xMin());
            float rayStepSizeY = delta.y() / (bbox.yMax() - bbox.yMin());
            float rayStepSizeZ = delta.z() / (bbox.zMax() - bbox.zMin());
            float rayStepSize = rayStepSizeX < rayStepSizeY ? rayStepSizeX : rayStepSizeY;
            rayStepSize = rayStepSize < rayStepSizeZ ? rayStepSize : rayStepSizeZ;

            spOctTree->setRayStepSize(rayStepSize);

            vox::SceneObject* pSceneObj = new vox::SceneObject(bbox.center(), QQuaternion());
            pSceneObj->setBoundingBox(bbox);

            CreateVertices(pSceneObj);
            
            QVector3D center = bbox.center();
            double radius = bbox.radius();
            pSceneObj->setBoundingSphere(vox::BoundingSphere(center, radius));

            maxTreeDepth = xmlStream.attributes().value("MaxDepth").toString().toInt();
                
            spOctTree->setDepth(maxTreeDepth);
            spOctTree->setSceneObject(pSceneObj);
        }
        else if(spOctTree.get() != nullptr &&
                xmlStream.name() == "Node")
        {
            fullyParsed = true;
            size_t rootIndexIsZero;
            spOctTree->getNodePool()->allocateChildNodeBlock(nullptr, rootIndexIsZero);
                
            NonConstantNodesTree nonConstantNodesTree(maxTreeDepth);
            if(!LoadOctTreeNode(xmlStream, filePath, 
                                nonConstantNodesTree, spOctTree.get(), 
                                rootIndexIsZero,
                                maxTreeDepth - 1))
            {
                std::cerr << "ERROR in LoadOctTreeNode." << std::endl;
                return nullptr;
            }

            xmlStream.skipCurrentElement();

            for(size_t i = 0;
                i < nonConstantNodesTree.size();
                ++i)
            {
                NonConstantNodes& nonConstantNodes = nonConstantNodesTree.at(i);
                if(nonConstantNodes.size() > 0)
                {
                    static std::string bricksFilePrefix = "voxels_";
                    static std::string bricksFileExt = ".gvb";
                    std::stringstream bricksFilePath;
                    bricksFilePath << filePath;
                    bricksFilePath << "/";
                    bricksFilePath << bricksFilePrefix;
                    bricksFilePath << i;
                    bricksFilePath << bricksFileExt;

                    if(!isBinary)// || !isCompressed)
                    {
                        std::cerr << "ERROR: binary bricks are only supported format at this point." << std::endl;
                        return nullptr;
                    }

                    if(!LoadBinaryBricks(bricksFilePath.str(),
                                         nonConstantNodes))
                    {
                        std::cerr << "ERROR: failed to load bricks " << bricksFilePath.str() << std::endl;
                        return nullptr;
                    }
                }
            }
        }
    }

    if(spOctTree.get() != nullptr && fullyParsed)
    {
        return spOctTree.release();
    }

    return nullptr;
}

gv::Node* GigaVoxelsReader::Load(const std::string& filename)
{
    //TODO move this into own "file" specific parser
    //implement gvx and gvb parsers
    QFile inputFile(QString(filename.c_str()));

    std::ifstream fileStream(filename);
    if(inputFile.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        return nullptr;
    }
    QXmlStreamReader xmlStream(&inputFile);
    
    std::string ext = vox::DataSetReader::GetFileExtension(filename);
    if(ext == "gvp")//Group node with multiple PagedOctTree nodes
        return LoadGVP(xmlStream, filename);
    else if(ext == "gvx")
    {
        GigaVoxelsOctTree* pOctTree = LoadGVX(xmlStream, filename);
        if(pOctTree != nullptr)
        {
            gv::OctTreeNode* pOctTreeNode = 
                new gv::OctTreeNode(pOctTree);

            const vox::BoundingSphere& bsphere = pOctTree->getSceneObject()->getBoundingSphere();
            pOctTreeNode->setBoundingSphere(bsphere.center(), bsphere.radius());

            return pOctTreeNode;
        }
    }

    return nullptr;
}

GigaVoxelsOctTree* GigaVoxelsReader::LoadOctTreeFile(const std::string& filename)
{
    QFile inputFile(QString(filename.c_str()));

    std::ifstream fileStream(filename);
    if(inputFile.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        return nullptr;
    }
    QXmlStreamReader xmlStream(&inputFile);

    GigaVoxelsOctTree* pOctTree = LoadGVX(xmlStream, filename);

    return pOctTree;
}