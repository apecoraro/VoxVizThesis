#include "GigaVoxels/GigaVoxelsRenderer.h"
#include "GigaVoxels/GigaVoxelsSceneGraph.h"
#include "GigaVoxels/GigaVoxelsReader.h"
#include "GigaVoxels/GigaVoxelsDatabasePager.h"
#include "GigaVoxels/GigaVoxelsBrickPool.h"

#include "GigaVoxels/GigaVoxelsShaderCodeTester.h"

#include "VoxVizCore/BoundingVolumes.h"
#include "VoxVizCore/Plane.h"
#include "VoxVizCore/Frustum.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"

#include <QtGui/QPainter>
#include <QtGui/qvector3d.h>
#include <QtOpenGL/qgl.h>

#include <QtGui/QPen>
#include <QtGui/QFont>
#include <QtGui/QColor>
#include <QtGui/QBrush>

#include <GL/gl.h>
#include <GL/glu.h>

#ifdef max
    #undef max
#endif

#include <iostream>
#include <algorithm>
#include <sstream>
#include <cmath>

using namespace gv;

static voxOpenGL::ShaderProgram* s_pDrawVolumeShader = NULL;
static voxOpenGL::ShaderProgram* s_pDrawVolumeWireFrameShader = NULL;
static voxOpenGL::ShaderProgram* s_pCameraNearPlaneShader = NULL;
static voxOpenGL::ShaderProgram* s_pCameraNearPlaneWireFrameShader = NULL;
static voxOpenGL::ShaderProgram* s_pGenerateSelectionMaskShader = NULL;
static voxOpenGL::ShaderProgram* s_pComputeActiveTexelsShader = NULL;
static voxOpenGL::ShaderProgram* s_pGenerateHistoPyramidShader = NULL;
static voxOpenGL::ShaderProgram* s_pCompressNodeUsageListShader = NULL;
static voxOpenGL::ShaderProgram* s_pDebugRendererShader = NULL;

void GigaVoxelsRenderer::RegisterRenderer()
{
	static vox::SmartPtr<GigaVoxelsRenderer> s_spRenderer = new GigaVoxelsRenderer();
}

void GigaVoxelsRenderer::setup()
{
    //create shader program
    if(getEnableLighting())
    {
        std::cout << "Setup with lighting." << std::endl;
        s_pDrawVolumeShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram("GigaVoxels.vert",
                                                                            "GigaVoxels.frag");
        s_pDrawVolumeWireFrameShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram("GigaVoxels.vert",
                                                                            "GigaVoxelsNodeWireFrame.frag");
        s_pCameraNearPlaneShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram(voxOpenGL::GLUtils::GetNearPlaneQuadVertexShaderFile(),
                                                                        "GigaVoxels.frag");

        s_pCameraNearPlaneWireFrameShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram(voxOpenGL::GLUtils::GetNearPlaneQuadVertexShaderFile(),
                                                                            "GigaVoxelsNodeWireFrame.frag");
    }
    else
    {
        std::cout << "Setup without lighting." << std::endl;
        s_pDrawVolumeShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram("GigaVoxels.vert",
                                                                            "GigaVoxelsNoLighting.frag");

        s_pDrawVolumeWireFrameShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram("GigaVoxels.vert",
                                                                            "GigaVoxelsNodeWireFrameNoLighting.frag");

        s_pCameraNearPlaneShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram(voxOpenGL::GLUtils::GetNearPlaneQuadVertexShaderFile(),
                                                                            "GigaVoxelsNoLighting.frag");

        s_pCameraNearPlaneWireFrameShader = 
            voxOpenGL::ShaderProgramManager::instance().createShaderProgram(voxOpenGL::GLUtils::GetNearPlaneQuadVertexShaderFile(),
                                                                            "GigaVoxelsNodeWireFrameNoLighting.frag");
    }

    s_pGenerateSelectionMaskShader = 
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ScreenAlignedTexturedQuad.vert",
                                                                        "GenerateSelectionMask.frag");

    s_pComputeActiveTexelsShader =
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ScreenAlignedTexturedQuad.vert",
                                                                        "ComputeActiveTexels.frag");

    s_pGenerateHistoPyramidShader = 
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ScreenAlignedTexturedQuad.vert",
                                                                        "GenerateHistoPyramid.frag");
    
    s_pCompressNodeUsageListShader =
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("ScreenAlignedTexturedQuad.vert",
                                                                        "CompressNodeUsageList.frag");

    s_pDebugRendererShader =
        voxOpenGL::ShaderProgramManager::instance().createShaderProgram("GigaVoxelsDebugRenderer.vert",
                                                                        "GigaVoxelsDebugRenderer.frag");
}

void GigaVoxelsRenderer::shutdown()
{
    DatabasePager::kill();
    GigaVoxelsOctTree::KillNodeUsageListProcessors();
    BrickPool::deleteInstance();
}

static void AddVertex(vox::FloatArray& vertexArray,
                      double x, double y, double z)
{
    vertexArray.push_back(static_cast<float>(x));
    vertexArray.push_back(static_cast<float>(y));
    vertexArray.push_back(static_cast<float>(z));
}

bool GigaVoxelsRenderer::acceptsFileExtension(const std::string& ext) const
{
    return ext == "pvm" || ext == "voxt" || ext == "gvp" || ext == "gvx";
}

static void InitDrawVolumeShader(voxOpenGL::ShaderProgram* pDrawVolumeShader)
{
    if(pDrawVolumeShader->bind())
    {
        pDrawVolumeShader->setUniformValue("OctTreeSampler", 0);

        pDrawVolumeShader->setUniformValue("BrickSampler", 1);

        pDrawVolumeShader->setUniformValue("BrickGradientsSampler", 2);

        pDrawVolumeShader->setUniformValue("JitterTexSampler", 3);
        pDrawVolumeShader->setUniformValue("JitterTexSize", 32);

		pDrawVolumeShader->setUniformUIntValue("ComputeLighting", true);

		pDrawVolumeShader->setUniformValue("LodScalar", 10.0f);

        pDrawVolumeShader->bindFragDataLocation("FragColor", 0);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[0]", 1);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[1]", 2);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[2]", 3);

		pDrawVolumeShader->release();
    }
}

static void InitDrawVolumeShaderNoLighting(voxOpenGL::ShaderProgram* pDrawVolumeShader)
{
    if(pDrawVolumeShader->bind())
    {
        pDrawVolumeShader->setUniformValue("OctTreeSampler", 0);

        pDrawVolumeShader->setUniformValue("BrickSampler", 1);

        pDrawVolumeShader->setUniformValue("JitterTexSampler", 2);
        pDrawVolumeShader->setUniformValue("JitterTexSize", 32);

		pDrawVolumeShader->setUniformValue("LodScalar", 10.0f);

        pDrawVolumeShader->bindFragDataLocation("FragColor", 0);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[0]", 1);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[1]", 2);
        pDrawVolumeShader->bindFragDataLocation("NodeUsageList[2]", 3);

		pDrawVolumeShader->release();
    }
}

class InitOctTrees : public gv::NodeVisitor
{
public:
    size_t poolDimX;
    size_t poolDimY;
    size_t poolDimZ;
    size_t pboSize;
    size_t borderVoxels;
    bool hasPagedOctTrees;
    bool hasData;
    bool hasCompressedBricks;
    bool hasUnsignedGradients;
    size_t brickDimX;
    size_t brickDimY;
    size_t brickDimZ;
    int vpWidth;
    int vpHeight;
    bool lightingEnabled;
    InitOctTrees(/*size_t poolX,
                    size_t poolY,
                    size_t poolZ,*/
                    size_t pbo,
                    size_t border,
                    int vpW, int vpH,
                    bool lightingEnabled) : 
        poolDimX(0),//poolX),
        poolDimY(0),//poolY),
        poolDimZ(0),//poolZ),
        pboSize(pbo),
        borderVoxels(border),
        hasPagedOctTrees(false), 
        hasData(false),
        hasCompressedBricks(false),
        hasUnsignedGradients(false),
        vpWidth(vpW), 
        vpHeight(vpH),
        lightingEnabled(lightingEnabled) {}

    virtual void apply(OctTreeNode& node) override
    {
        if(node.getOctTree() != nullptr)
        {
            hasData = true;
            node.getOctTree()->getSceneObject()->createVAO();
            node.getOctTree()->createNodeUsageTextures(vpWidth, vpHeight);
            node.getOctTree()->getBrickParams(brickDimX, brickDimY, brickDimZ,
                                                hasCompressedBricks,
                                                hasUnsignedGradients);

            if(BrickPool::initialized() == false)
            {
                if(hasCompressedBricks)
                {
                    if(brickDimX > 14 || brickDimY > 14 || brickDimZ > 14)
                    {
                        poolDimX = 24;
                        poolDimY = 24;
                        poolDimZ = 24;
                    }
                    else
                    {
                        poolDimX = 48;
                        poolDimY = 48;
                        poolDimZ = 48;
                    }
                    BrickPool::instance().initSpecial(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                                                        GL_UNSIGNED_BYTE,
                                                        GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                                        GL_UNSIGNED_BYTE,
                                                        hasCompressedBricks,
                                                        brickDimX, brickDimY, brickDimZ,
                                                        poolDimX, poolDimY, poolDimZ,
                                                        pboSize,
                                                        borderVoxels);
                }
                else
                {
                    if(brickDimX > 14 || brickDimY > 14 || brickDimZ > 14)
                    {
                        poolDimX = 8;
                        poolDimY = 8;
                        poolDimZ = 8;
                    }
                    else
                    {
                        poolDimX = 16;
                        poolDimY = 16;
                        poolDimZ = 16;
                    }
                    BrickPool::instance().initSpecial(GL_RGBA8,
                                                GL_UNSIGNED_BYTE,
                                                GL_RGB8,
                                                GL_UNSIGNED_BYTE,
                                                hasCompressedBricks,
                                                brickDimX, brickDimY, brickDimZ,
                                                poolDimX, poolDimY, poolDimZ,
                                                pboSize,
                                                borderVoxels);
                }
            }
                    
            node.getOctTree()->uploadInitialBricks();
            node.getOctTree()->initNodePoolAndNodeUsageListProcessor();
        }
    }

    virtual void apply(PagedOctTreeNode& node) override
    {
        hasData = true;
        hasPagedOctTrees = true;
        //this data should be consistent so grabbing it from
        //each node should yield same result
        node.getOctTreeData(hasCompressedBricks,
                            brickDimX, brickDimY, brickDimZ);
        if(hasCompressedBricks)
        {
            if(brickDimX > 14 || brickDimY > 14 || brickDimZ > 14)
            {
                if(sizeof(char*) == 4)
                {
                    poolDimX = 20;
                    poolDimY = 20;
                    poolDimZ = 20;
                }
                else
                {
                    poolDimX = !this->lightingEnabled ? 32 : 24;
                    poolDimY = !this->lightingEnabled ? 32 : 24;
                    poolDimZ = !this->lightingEnabled ? 32 : 24;
                }
            }
            else
            {
                poolDimX = 48;
                poolDimY = 48;
                poolDimZ = 48;
            }
            
        }
        else
        {
            if(brickDimX > 14 || brickDimY > 14 || brickDimZ > 14)
            {
                poolDimX = 8;
                poolDimY = 8;
                poolDimZ = 8;
            }
            else
            {
                poolDimX = 16;
                poolDimY = 16;
                poolDimZ = 16;
            }
        }
    }
};

void GigaVoxelsRenderer::init(vox::Camera& camera,
                             vox::SceneObject& scene)
{
    vox::VolumeDataSet* pVoxels = dynamic_cast<vox::VolumeDataSet*>(&scene);

    if(pVoxels == NULL)
        return;

	setDrawDebug(1);
    
    int width, height;
    camera.getViewportWidthHeight(width, height);
    
    const std::string inputFile = pVoxels->getInputFile();
    if(GigaVoxelsReader::IsGigaVoxelsFile(inputFile))
    {
        GigaVoxelsReader::SetLoadNormals(getEnableLighting());
        //build a tree of oct tree
        vox::SmartPtr<gv::Node> spNode = 
                        GigaVoxelsReader::Load(inputFile);
        
        pVoxels->setUserData(spNode.get());

        //size_t poolDimX = 48;
        //size_t poolDimY = 48;
        //size_t poolDimZ = 48;
        size_t pboSize = 100000000;//100 MB
        size_t border = 2;
        InitOctTrees vis(//poolDimX, poolDimY, poolDimZ, 
                         pboSize, border, width, height,
                         getEnableLighting());

        spNode->accept(vis);

        if(vis.hasData == false)
            return;

        if(vis.hasPagedOctTrees)
        {
            if(BrickPool::initialized() == false)
            {
                if(vis.hasCompressedBricks)
                    BrickPool::instance().initSpecial(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
                                                      GL_UNSIGNED_BYTE,
                                                      GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                                      GL_UNSIGNED_BYTE,
                                                      vis.hasCompressedBricks,
                                                      vis.brickDimX, vis.brickDimY, vis.brickDimZ,
                                                      vis.poolDimX, vis.poolDimY, vis.poolDimZ,
                                                      pboSize,
                                                      border,
                                                      getEnableLighting());
                else
                    BrickPool::instance().initSpecial(GL_RGBA8,
                                                      GL_UNSIGNED_BYTE,
                                                      GL_RGB8,
                                                      GL_UNSIGNED_BYTE,
                                                      vis.hasCompressedBricks,
                                                      vis.brickDimX, vis.brickDimY, vis.brickDimZ,
                                                      16, 16, 16,
                                                      pboSize,
                                                      border,
                                                      getEnableLighting());
                
				voxOpenGL::GLUtils::CheckOpenGLError();

                BrickPool::instance().initEmptyBricks();
            }
            m_pDatabasePager = &DatabasePager::instance();
            
            int width, height;
            camera.getViewportWidthHeight(width, height);
            m_pDatabasePager->start((QGLContext*)getBackgroundGLContext(),
                                    width, height);

            m_debugRenderer.init(camera, *spNode.get(),
                                 s_pDebugRendererShader);
        }
    }
    else
    {
        qreal intensity = 64.0/255.0;
        qreal alpha = 64.0/255.0;
        QVector4D colors[] =
        {
             QVector4D(0,  0,  0,  0),//transparent black
             QVector4D(0, intensity,  0, alpha),//red
             QVector4D(intensity,  0,  0, alpha),//green
             QVector4D(0,  0, intensity, alpha) //blue
        };

        vox::VolumeDataSet::ColorLUT colorLUT(&colors[0], &colors[4]);

        GigaVoxelsOctTree* pSVO = new GigaVoxelsOctTree();
    
        pSVO->build(pVoxels, colorLUT);

        pVoxels->setUserData(pSVO);

        //create textures and geometry used by ray cast oct-tree volume renderer
        //vox::TextureIDArray& textureIDs = pVoxels->getTextureIDArray();

        //textureIDs.reserve(1);
        //textureIDs.resize(1);

        ////also create a color lookup table 1D texture
        //unsigned char colorLUT[] = {
        //     0,  0,  0,  0,//transparent black
        //     0, 64,  0, 64,//green
        //    64,  0,  0, 64,//red
        //     0,  0, 64, 64 //blue
        //};

        ////create textures used by volume renderer
        //textureIDs[0] = voxOpenGL::GLUtils::Create1DTexture(colorLUT, static_cast<int>(sizeof(colorLUT) / (sizeof(unsigned char) * 4.0f)));

        vox::FloatArray& vertexArray = pVoxels->getFloatVertexArray();
    
        const vox::BoundingBox& bbox = pVoxels->getBoundingBox();

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

        pVoxels->setPrimitiveType(GL_QUADS);//TODO switch to GL_TRIANGLES
        pVoxels->createVAO();

        //this texture tracks the node usage
        pSVO->createNodeUsageTextures(width, height);
    }
    
    //use this fbo to render to the textures we create next
    m_spFrameBufferObject = new voxOpenGL::GLFrameBufferObject(width, 
                                                               height);
    
    float* pZero = new float[width * height * 4];
    memset(pZero, 0, sizeof(float) * width * height * 4);
    //this texture is the volume render texture
    m_colorTextureID = 
        voxOpenGL::GLUtils::Create2DTexture(GL_RGBA32F,
                                            width, height,
                                            GL_RGBA,
                                            GL_FLOAT,
                                            pZero,
                                            false,
                                            false);
    delete [] pZero;
    
    m_jitterTextureID =
        voxOpenGL::GLUtils::Create2DJitterTexture(32, 32);

    if(getEnableLighting())
    {
        s_pDrawVolumeShader->setOnReloadFunc(InitDrawVolumeShader);
        InitDrawVolumeShader(s_pDrawVolumeShader);
    
        s_pCameraNearPlaneShader->setOnReloadFunc(InitDrawVolumeShader);
        InitDrawVolumeShader(s_pCameraNearPlaneShader);

        s_pDrawVolumeWireFrameShader->setOnReloadFunc(InitDrawVolumeShader);
        InitDrawVolumeShader(s_pDrawVolumeWireFrameShader);
    
        s_pCameraNearPlaneWireFrameShader->setOnReloadFunc(InitDrawVolumeShader);
        InitDrawVolumeShader(s_pCameraNearPlaneWireFrameShader);
    }
    else
    {
        s_pDrawVolumeShader->setOnReloadFunc(InitDrawVolumeShaderNoLighting);
        InitDrawVolumeShaderNoLighting(s_pDrawVolumeShader);
    
        s_pCameraNearPlaneShader->setOnReloadFunc(InitDrawVolumeShaderNoLighting);
        InitDrawVolumeShaderNoLighting(s_pCameraNearPlaneShader);

        s_pDrawVolumeWireFrameShader->setOnReloadFunc(InitDrawVolumeShaderNoLighting);
        InitDrawVolumeShaderNoLighting(s_pDrawVolumeWireFrameShader);
    
        s_pCameraNearPlaneWireFrameShader->setOnReloadFunc(InitDrawVolumeShaderNoLighting);
        InitDrawVolumeShaderNoLighting(s_pCameraNearPlaneWireFrameShader);
    }
    
    if(s_pGenerateSelectionMaskShader->bind())
    {
        s_pGenerateSelectionMaskShader->setUniformValue("NodeUsageListSampler", 0);

        s_pGenerateSelectionMaskShader->release();
    }

    //compute active texels generates level 0 of the histopyramid texture
    if(s_pComputeActiveTexelsShader->bind())
    {
        s_pComputeActiveTexelsShader->setUniformValue("SelectionMaskSampler", 0);
        
        s_pComputeActiveTexelsShader->release();
    }

    //this shader generates level 1 through m_histPyramidLevelCount-1
    if(s_pGenerateHistoPyramidShader->bind())
    {
        s_pGenerateHistoPyramidShader->setUniformValue("InputTextureSampler", 0);

        s_pGenerateHistoPyramidShader->release();
    }

    if(s_pCompressNodeUsageListShader->bind())
    {
        s_pCompressNodeUsageListShader->setUniformValue("SelectionMaskHistoPyramidSampler", 0);
        int pyramidLevel = 0;
        while(true)
        {
            if(width == 1 && height == 1)
                break;

            if(width != 1)
                width >>= 1;
            if(height != 1)
                height >>= 1;

            ++pyramidLevel;
        }

        int levelCount = pyramidLevel + 1;
    
        s_pCompressNodeUsageListShader->setUniformValue("SelectionMaskHistoPyramidLevelCount",
                                                        levelCount);

        s_pCompressNodeUsageListShader->setUniformValue("NodeUsageListSampler", 1);
        s_pCompressNodeUsageListShader->setUniformValue("SelectionMaskSampler", 2);

        s_pCompressNodeUsageListShader->release();
    }

    voxOpenGL::GLUtils::InitScreenAlignedQuad();

    voxOpenGL::GLUtils::InitNearPlaneQuad(camera);
}

GigaVoxelsRenderer::GigaVoxelsRenderer() :
    vox::Renderer("gv"),
    m_lodScalar(1.0f),
    m_colorTextureID(0),
    m_pDrawVolumeShader(nullptr),
    m_pCameraNearPlaneShader(nullptr),
    m_pRenderList(nullptr),
    m_pUpdateList(nullptr),
    m_pDatabasePager(nullptr),
    m_stateBtnTextSize(10.0f),
    m_valueTextSize(m_stateBtnTextSize),
        //database pager UI elements
    m_pDbPagerUILabel(nullptr),
    m_pDbPagerStateBtn(nullptr),
    m_pDbPagerNumPendingLoadLabel(nullptr),
    m_pDbPagerNumPendingLoadValueBar(nullptr),
    m_pDbPagerNumPendingLoadText(nullptr),
    m_pDbPagerNumActiveLabel(nullptr),
    m_pDbPagerNumActiveValueBar(nullptr),
    m_pDbPagerNumActiveText(nullptr),
    //m_pDbPagerMaxLoadTimeValueText(nullptr),
    //m_pDbPagerMinLoadTimeValueText(nullptr),
    //m_pDbPagerAvgLoadTimeValueText(nullptr),
    //gpu node usage list processor UI elements
    m_pNodeListProcUILabel(nullptr),
    m_nodeListProcUILeft(600.0f),
    m_nodeListProcUIStateBtnTop(700.0f),
    m_nodeListProcUIElementIncr(m_stateBtnTextSize),
    //brick upload list
    m_pBrickUploadUILabel(nullptr),
    m_pBrickUploadListValueBar(nullptr),
    m_pBrickUploadListText(nullptr)
{
}

GigaVoxelsRenderer::~GigaVoxelsRenderer()
{
    glDeleteTextures(1, &m_colorTextureID);
    if(m_pDatabasePager != nullptr)
    {
        m_pDatabasePager->kill();
    }
}

void GigaVoxelsRenderer::setDebugShader(bool flag)
{
    if(flag)
    {
        m_pDrawVolumeShader = s_pDrawVolumeWireFrameShader;
        m_pCameraNearPlaneShader = s_pCameraNearPlaneWireFrameShader;
    }
    else
    {
        m_pDrawVolumeShader = s_pDrawVolumeShader;
        m_pCameraNearPlaneShader = s_pCameraNearPlaneShader;
    }
}

class CullVisitor : public gv::NodeVisitor
{
private:
    size_t m_frameIndex;
    vox::Frustum m_frustum;
    gv::GigaVoxelsRenderer::Drawable* m_pRenderList;
    gv::GigaVoxelsRenderer::DrawablePool& m_drawablePool;
    size_t m_nextFreeDrawable;
    gv::DatabasePager* m_pDatabasePager;
    gv::GigaVoxelsDebugRenderer* m_pDebugRenderer;
public:
    CullVisitor(vox::Camera& camera,
                gv::GigaVoxelsRenderer::DrawablePool& drawablePool,
                gv::DatabasePager* pPager,
                gv::GigaVoxelsDebugRenderer* pDbgRenderer) :
        m_frameIndex(camera.getFrameCount()),
        m_pRenderList(nullptr),
        m_drawablePool(drawablePool),
        m_nextFreeDrawable(0),
        m_pDatabasePager(pPager),
        m_pDebugRenderer(pDbgRenderer)
    {
        m_frustum.setFromCamera(camera);
    }

    gv::GigaVoxelsRenderer::Drawable* getRenderList() { return m_pRenderList; }

    virtual void apply(Node& node) override
    {
        float nearPlaneDist;
        vox::Frustum::RESULT result =
            m_frustum.sphereInFrustum(node.center(), node.radius(), nearPlaneDist);
        if(result != vox::Frustum::OUTSIDE)
            traverse(node);
    }

    gv::GigaVoxelsRenderer::Drawable* getNextFreeDrawable(float distToNearPlane, GigaVoxelsOctTree* pOctTree)
    {
        if(m_nextFreeDrawable >= m_drawablePool.size())
        {
            return nullptr;
        }

        gv::GigaVoxelsRenderer::Drawable* pDrawable = &m_drawablePool[m_nextFreeDrawable];

        ++m_nextFreeDrawable;

        pDrawable->distToNearPlane = distToNearPlane;
        pDrawable->spOctTree = pOctTree;
        pDrawable->pNext = nullptr;

        return pDrawable;
    }

    void insertIntoRenderList(float distToNearPlane, GigaVoxelsOctTree* pOctTree)
    {
        if(m_pRenderList == nullptr)
        {
            m_pRenderList = getNextFreeDrawable(distToNearPlane, pOctTree);
            return;
        }
        else if(m_pRenderList->distToNearPlane < distToNearPlane)
        {
            gv::GigaVoxelsRenderer::Drawable* pNewHead = getNextFreeDrawable(distToNearPlane, pOctTree);
            pNewHead->pNext = m_pRenderList;
            m_pRenderList = pNewHead;
            return;
        }

        gv::GigaVoxelsRenderer::Drawable* pPrev = m_pRenderList;
        gv::GigaVoxelsRenderer::Drawable* pDrawable = m_pRenderList->pNext;

        for( ; pDrawable != nullptr; 
            pDrawable = pDrawable->pNext, pPrev = pPrev->pNext)
        {
            if(pDrawable->distToNearPlane < distToNearPlane)
            {
                pPrev->pNext = getNextFreeDrawable(distToNearPlane, pOctTree);
                pPrev->pNext->pNext = pDrawable;
                return;
            }
        }

        pPrev->pNext = getNextFreeDrawable(distToNearPlane, pOctTree);
    }
        
    virtual void apply(OctTreeNode& node) override
    {
        GigaVoxelsOctTree* pOctTree = node.getOctTree();
        if(pOctTree != nullptr)
        {
            float nearPlaneDist;
            vox::Frustum::RESULT result =
                    m_frustum.sphereInFrustum(node.center(),
                                              node.radius(),
                                              nearPlaneDist);
            if(result != vox::Frustum::OUTSIDE)
            {
                nearPlaneDist -= node.radius();
                insertIntoRenderList(nearPlaneDist, pOctTree);
            }
        }

        //traverse(node);
    }

    virtual void apply(PagedOctTreeNode& node) override
    {
        float distToNearPlane;
        vox::Frustum::RESULT result =
                    m_frustum.sphereInFrustum(node.center(), node.radius(), distToNearPlane);
        if(result != vox::Frustum::OUTSIDE)
        {
            node.setLastAccess(m_frameIndex);

            vox::SmartPtr<GigaVoxelsOctTree> spOctTree = node.getOctTree();
            if(spOctTree.get() == nullptr)
            {
                m_pDatabasePager->requestLoadOctTree(node, distToNearPlane);
                m_pDebugRenderer->notifyPagedNodeIsLoading(node);
            }
            else
            {
                //float distToNearPlane = m_frustum.computeDistToNearPlane(node.center());
                distToNearPlane -=  node.radius();
                insertIntoRenderList(distToNearPlane, spOctTree.get());
            }
        }

        //traverse(node);
    }
};

//void GigaVoxelsRenderer::getStatus(std::string& statusText) const
//{
//    //the database pager state (active, idle), num pending load, num pending merge, num active
//    //max time to load, min time to load, avg time to load
//    if(m_pDatabasePager != NULL)
//    {
//        DatabasePager::State state;
//        size_t numPendingLoad;
//        size_t numPendingMerge;
//        size_t numLoaded;
//        size_t maxLoadTimeMs, minLoadTimeMs;
//        float avgLoadTimeMs;
//        m_pDatabasePager->getStatus(state,
//                                    numPendingLoad,
//                                    numPendingMerge,
//                                    numLoaded,
//                                    maxLoadTimeMs,
//                                    minLoadTimeMs,
//                                    avgLoadTimeMs);
//        std::stringstream pagerStatus;
//        pagerStatus << "Octree Disk Pager State: ";
//        switch(state)
//        {
//        case DatabasePager::INIT:
//            pagerStatus << "INIT ";
//            break;
//        case DatabasePager::WAITING:
//            pagerStatus << "WAIT ";
//            break;
//        case DatabasePager::LOADING:
//            pagerStatus << "ACTIVE ";
//            break;
//        case DatabasePager::EXITING:
//            pagerStatus << "EXIT ";
//            break;
//        }
//
//        if(avgLoadTimeMs > 0.0f)
//        {
//            pagerStatus << "Loading: " << numPendingLoad << " "
//                        << "Merging: " << numPendingMerge << " "
//                        << "Loaded: " << numLoaded << " ";
//
//            pagerStatus << "Load Time Avg: " << avgLoadTimeMs << "ms "
//                        << "Min: " << minLoadTimeMs << "ms "
//                        << "Max: " << maxLoadTimeMs << "ms ";
//
//        }
//        statusText += pagerStatus.str();
//    }
//    
//    //each usage list processor state with avg process time / num items in list.
//    std::string nodeListProcStatus;
//    GigaVoxelsOctTree::GetNodeUsageListProcessorsStatus(nodeListProcStatus);
//
//    statusText += nodeListProcStatus;
//}

class Timer
{
private:
    typedef std::map<std::string, std::pair<QElapsedTimer, qint64> > Timers;
    Timers m_timers;

public:
    Timer()
    {
        m_timers["drawVolume"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["generateNodeUsageSelectionMask"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["generateSelectionMaskHistoPyramid"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["DrawScreenAlignedQuad"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["DrawGUI"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["compressNodeUsageList"] = std::make_pair(QElapsedTimer(), 0);
        m_timers["getCompressedNodeUsageList"] = std::make_pair(QElapsedTimer(), 0);
    }

    qint64 total()
    {
        qint64 total = 0;
        for(Timers::iterator itr = m_timers.begin();
            itr != m_timers.end();
            ++itr)
        {
            total += itr->second.second;
            itr->second.second = 0u;
        }

        return total;
    }

    void startTimer(const std::string& timer)
    {
        m_timers[timer].first.start();
    }

    void stopTimer(const std::string& timer)
    {
        std::pair<QElapsedTimer, qint64>& timerPair = m_timers[timer];
        qint64 elapsed = timerPair.first.elapsed();
        timerPair.second += elapsed;
    }
};

static Timer s_timer;

void GigaVoxelsRenderer::draw(vox::Camera& camera,
                             vox::SceneObject& scene)
{
    vox::VolumeDataSet* pDataSet = dynamic_cast<vox::VolumeDataSet*>(&scene);
	
	if(pDataSet == NULL)
        return;

    if(getReloadShaders())
    {
        s_pDrawVolumeShader->reloadShaderFiles();
        s_pCameraNearPlaneShader->reloadShaderFiles();
        s_pDrawVolumeWireFrameShader->reloadShaderFiles();
        s_pCameraNearPlaneWireFrameShader->reloadShaderFiles();
        setReloadShaders(false);
    }

    voxOpenGL::GLUtils::CheckOpenGLError();

    int debugMode = getDrawDebug();
    setDebugShader(debugMode == 2 || debugMode == 3 || debugMode == 4);

    qint64 gpuTimeMs = m_gpuTimer.elapsed();
    m_pGPUTimeUI->SetValue(static_cast<uint32_t>(gpuTimeMs));

    QElapsedTimer drawTimer;
    drawTimer.start();

    m_spFrameBufferObject->bind();

    qint64 cullTimeMs = 0;

    m_pUpdateList = nullptr;
    m_pRenderList = nullptr;

    vox::SmartPtr<GigaVoxelsOctTree> spOctTree = dynamic_cast<GigaVoxelsOctTree*>(pDataSet->getUserData());
    if(spOctTree.get() != NULL)
    {
        spOctTree->setSceneObject(pDataSet);
        Drawable* pDrawable = nullptr;
        if(m_octTreeDrawablePool.size() == 0)
            m_octTreeDrawablePool.resize(1);
        pDrawable = &m_octTreeDrawablePool[0];
        pDrawable->distToNearPlane = 0.0f;
        pDrawable->spOctTree = spOctTree.get();
        pDrawable->pNext = nullptr;
        m_pRenderList = pDrawable;
    }
    else
    {
        vox::SmartPtr<gv::Node> spNode = dynamic_cast<gv::Node*>(pDataSet->getUserData());
        if(spNode.get() != nullptr)
        {
            QElapsedTimer cullTimer;
            cullTimer.start();

            if(m_octTreeDrawablePool.size() == 0)
                m_octTreeDrawablePool.resize(100);
     
            
            CullVisitor culler(camera, 
                               m_octTreeDrawablePool, 
                               m_pDatabasePager,
                               &m_debugRenderer);

            spNode->accept(culler);

            m_pRenderList = culler.getRenderList();

            cullTimeMs = cullTimer.elapsed();
            m_pCullTimeUI->SetValue(static_cast<uint32_t>(cullTimeMs));
        }
        else
            return;
    }

    voxOpenGL::GLUtils::CheckOpenGLError();

    if(m_pRenderList == nullptr)
    {
        m_spFrameBufferObject->release();
        if(debugMode == 1 || debugMode == 2 || debugMode == 4)
        {
            s_timer.startTimer("DrawGUI");

            m_debugRenderer.draw(camera);

            s_timer.stopTimer("DrawGUI");
        }
        return;
    }

    //attach color texture and depth stencil and clear them
    m_spFrameBufferObject->mapDrawBuffers(1);
    m_spFrameBufferObject->attachColorBuffer(m_colorTextureID, 0);
    m_spFrameBufferObject->attachDepthStencilBuffer();
    m_spFrameBufferObject->setViewportWidthHeight(camera.getViewportWidth(),
                                                  camera.getViewportHeight());

    //clear the color buffer
    static GLfloat zeroFloat[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glClearBufferfv(GL_COLOR, 0, zeroFloat);

    //clear depth buffer?
    static GLfloat depth = 1.0f;
    static GLint stencil = 0;
    glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);

    voxOpenGL::GLUtils::CheckOpenGLError();

    //enable depth test, but don't write to it
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glEnable(GL_BLEND);
    //first time through we overwrite 
    //glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for(Drawable* pDrawable = m_pRenderList;
        pDrawable != nullptr;
        pDrawable = pDrawable->pNext)
    {
        vox::SmartPtr<GigaVoxelsOctTree> spSVO = pDrawable->spOctTree.get();
        if(spSVO->getRootNode()->getBrickIsOnGpuFlag() == false)
        {
            if(spSVO->getRootNode()->getNodeTypeFlag() != gv::GigaVoxelsOctTree::Node::CONSTANT_NODE)
            {
                std::cerr << "Root brick in render list is not on GPU?" << std::endl;
                continue;
            }
        }

        float distToNearPlane = pDrawable->distToNearPlane;
        bool cameraIsPossiblyInside = distToNearPlane <= 0.0f;

        vox::SmartPtr<vox::SceneObject> spVoxels = spSVO->getSceneObject();

        size_t numSamples = pDataSet->getNumSamples();
        if(numSamples == 0)
        {
            size_t svoDimX, svoDimY, svoDimZ;
            if(spSVO->getMipMapDimensions(0, svoDimX, svoDimY, svoDimZ))
            {
                numSamples = static_cast<size_t>(std::sqrt(static_cast<float>(
                                        (svoDimX * svoDimX)
                                        + (svoDimY * svoDimY)
                                        + (svoDimZ * svoDimZ)))) + 2;
            
            }
            if(numSamples < 32)
                numSamples = 32;
        }
        //step 1 draw volume to color buffer and
        //generate initial node usage textures
        s_timer.startTimer("drawVolume");

        drawVolume(camera, *spVoxels.get(), *spSVO.get(), numSamples, cameraIsPossiblyInside);

		s_timer.stopTimer("drawVolume");

		voxOpenGL::GLUtils::CheckOpenGLError();
        
        m_spFrameBufferObject->mapDrawBuffers(1);
    
        s_timer.startTimer("generateNodeUsageSelectionMask");
        //step 2 generate a selection mask used to
        //filter the node usage textures so that we
        //don't have duplicates in the node usage textures
        generateNodeUsageSelectionMask(*spSVO.get(), 
                                       camera);

        s_timer.stopTimer("generateNodeUsageSelectionMask");

        s_timer.startTimer("generateSelectionMaskHistoPyramid");        
        //step 3 compress node usage list using the histopyramid algorithm
        //to compress the selection mask then using the compressed
        //selections mask we select only the unique nodes for each pixel/ray
        
        //we'll download the result of this operation in the next loop
        generateSelectionMaskHistoPyramid(*spSVO.get(),
                                          camera);

        s_timer.stopTimer("generateSelectionMaskHistoPyramid");

        //glBlendFunc(GL_ONE_MINUS_DST_ALPHA, GL_ONE);
    }

    voxOpenGL::GLUtils::CheckOpenGLError();
    
    m_spFrameBufferObject->release();

    voxOpenGL::GLUtils::CheckOpenGLError();

    //render the volume on screen using 
    //bind color buffer to screen aligned quad
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    s_timer.startTimer("DrawScreenAlignedQuad");
    
    voxOpenGL::GLUtils::BindScreenAlignedTexturedQuadShader(m_colorTextureID);

    bool colorTest = false;
    if(colorTest)
    {
        int width, height, depth;
        float* pColors = 
            (float*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D,
                                                                    width, height, depth,
                                                                    GL_RGBA, GL_FLOAT);
        float red = pColors[0];
        float green = pColors[1];
        float blue = pColors[2];
        float alpha = pColors[3];

        delete [] pColors;
    }
    
    voxOpenGL::GLUtils::DrawScreenAlignedQuad();

    voxOpenGL::GLUtils::UnbindScreenAlignedTexturedQuadShader();

    glDepthMask(GL_TRUE);

    s_timer.stopTimer("DrawScreenAlignedQuad");

    if(debugMode == 1 || debugMode == 2 || debugMode == 4)
    {
        s_timer.startTimer("DrawGUI");

        m_debugRenderer.draw(camera);

        s_timer.stopTimer("DrawGUI");
    }

    qint64 totalElapsed = drawTimer.elapsed();

    qint64 totalMini = s_timer.total();

    //take out the cull time
    qint64 drawTimeMs = (totalElapsed - cullTimeMs);
    m_pDrawTimeUI->SetValue(static_cast<uint32_t>(drawTimeMs));
}

void GigaVoxelsRenderer::postDraw(vox::Camera& camera,
                                  vox::SceneObject& scene)
{
    m_gpuTimer.start();//include time to read back from gpu in gpu time

    QElapsedTimer postDrawTimer;
    postDrawTimer.start();

    m_spFrameBufferObject->bind();
    
    if(camera.getFrameCount() > 0 && m_pUpdateList != nullptr)
    {
        m_spFrameBufferObject->mapDrawBuffers(1);
        //Do this in seperate loop because it requires reading back
        //data generated in last loop and doing it in first loop
        //would cause render queue flush
        for(Drawable* pDrawable = m_pUpdateList;
            pDrawable != nullptr;
            pDrawable = pDrawable->pNext)
        {
            GigaVoxelsOctTree* pSVO = pDrawable->spOctTree.get();

            compressNodeUsageList(*pSVO, camera);

            pDrawable->spOctTree = nullptr;
        }

        int width, height;
        camera.getViewportWidthHeight(width, height);
        //restore framebuffer width and height for next stage
        m_spFrameBufferObject->setViewportWidthHeight(width, height);
    }

    m_spFrameBufferObject->release();

    qint64 postDrawTime = postDrawTimer.elapsed();
}

void GigaVoxelsRenderer::initGUI(NvUIWindow& guiWindow)
{
    float width = guiWindow.GetWidth();
    float height = guiWindow.GetHeight();

    float m_valueTextSize = 20.0f;
    float nextLineIncr = m_valueTextSize * 1.15f;

    float timerUILeft = 10.0f;
    float timerUITop = 10.0f;
    //fps is at 10.0f so increment now
    m_pDrawTimeUI = vox::Renderer::CreateGUIValueText("Draw Time (ms): ", m_valueTextSize, 0u);
    timerUITop += nextLineIncr;
    guiWindow.Add(m_pDrawTimeUI, timerUILeft, timerUITop);
    //if pager then we have cull time
    if(m_pDatabasePager != NULL)
    {
        m_pCullTimeUI = vox::Renderer::CreateGUIValueText("Cull Time (ms): ", m_valueTextSize, 0u);
        timerUITop += nextLineIncr;
        guiWindow.Add(m_pCullTimeUI, timerUILeft, timerUITop);
    }

    m_pUpdateTimeUI = vox::Renderer::CreateGUIValueText("Update Time (ms): ", m_valueTextSize, 0u);
    timerUITop += nextLineIncr;
    guiWindow.Add(m_pUpdateTimeUI, timerUILeft, timerUITop);

    m_pGPUTimeUI = vox::Renderer::CreateGUIValueText("GPU Time (ms): ", m_valueTextSize, 0u);
    timerUITop += nextLineIncr;
    guiWindow.Add(m_pGPUTimeUI, timerUILeft, timerUITop);
    //initialize gpu timer
    m_gpuTimer.start();

    //parameters for right side UI
    float uiLineHeight = 125.0f;
    float offsetUIElement = 7.0f;

    float offsetLabelsWidth = 100.0f;
    float offsetUIElementWidth = 100.f;

    float stateBtnWidth = 30.0f;
    float stateBtnHeight = 30.0f;
    float stateBtnFontFactor = 0.55f;

    float valueBarWidth = offsetUIElementWidth - 25.0f;
    float valueBarHeight = 10.0f;
    float valueBarMax = 30.0f;
    float valueBarMin = 0.0f;
    float valueBarStep = 1.0f;
    float valueBarInitValue = 0.0f;
    float valueBarTextValueOffsetWidth = 10.0f;

    m_stateBtnTextSize = stateBtnHeight * stateBtnFontFactor;
        
    if(m_pDatabasePager != NULL)
    {
        //the database pager state (active, idle), num pending load, num pending merge, num active
        m_pDbPagerUILabel = vox::Renderer::CreateGUIText("Database Pager Thread: ", m_valueTextSize);
        guiWindow.Add(m_pDbPagerUILabel, 
                      width - offsetLabelsWidth,
                      uiLineHeight);

        m_pDbPagerStateBtn = vox::Renderer::CreateGUIButton("IDLE",
                                                            stateBtnWidth,
                                                            stateBtnHeight, 
                                                            2,//num states==2
                                                            m_stateBtnTextSize);

        guiWindow.Add(m_pDbPagerStateBtn,
                      width - offsetUIElementWidth + 5.0f,
                      uiLineHeight);

        uiLineHeight += nextLineIncr + 10.0f;

        m_pDbPagerNumPendingLoadLabel = vox::Renderer::CreateGUIText("Pending Load: ", m_valueTextSize);
        guiWindow.Add(m_pDbPagerNumPendingLoadLabel,
                      width - offsetLabelsWidth,
                      uiLineHeight);
    
        m_pDbPagerNumPendingLoadValueBar = vox::Renderer::CreateGUIValueBar(valueBarWidth, valueBarHeight,
                                                                            valueBarMax, valueBarMin,
                                                                            valueBarInitValue);
        guiWindow.Add(m_pDbPagerNumPendingLoadValueBar,
                      width - offsetUIElementWidth,
                      uiLineHeight + offsetUIElement);

        m_pDbPagerNumPendingLoadText = vox::Renderer::CreateGUIText("", m_valueTextSize);
        guiWindow.Add(m_pDbPagerNumPendingLoadText,
                      width - valueBarTextValueOffsetWidth,
                      uiLineHeight);

        uiLineHeight += nextLineIncr;

        m_pDbPagerNumActiveLabel = vox::Renderer::CreateGUIText("Active: ", m_valueTextSize);
        guiWindow.Add(m_pDbPagerNumActiveLabel,
                      width - offsetLabelsWidth,
                      uiLineHeight);
    
        m_pDbPagerNumActiveValueBar = vox::Renderer::CreateGUIValueBar(valueBarWidth, valueBarHeight,
                                                                       valueBarMax, valueBarMin,
                                                                       valueBarInitValue);
        guiWindow.Add(m_pDbPagerNumActiveValueBar,
                      width - offsetUIElementWidth,
                      uiLineHeight + offsetUIElement);

        m_pDbPagerNumActiveText = vox::Renderer::CreateGUIText("", m_valueTextSize);
        guiWindow.Add(m_pDbPagerNumActiveText,
                      width - valueBarTextValueOffsetWidth,
                      uiLineHeight);

        uiLineHeight += nextLineIncr;

        /*NvUIText* pDbPagerMaxLoadTimeLabel = vox::Renderer::CreateGUIText("Max Load Time (ms): ", m_valueTextSize);
        guiWindow.Add(pDbPagerMaxLoadTimeLabel,
                      width - offsetLabelsWidth,
                      uiLineHeight + offsetUIElement);


        m_pDbPagerMaxLoadTimeValueText = vox::Renderer::CreateGUIText("0", m_valueTextSize);
        guiWindow.Add(m_pDbPagerMaxLoadTimeValueText,
                      width - offsetUIElementWidth + 45,
                      uiLineHeight + offsetUIElement);

        uiLineHeight += nextLineIncr;

        NvUIText* pDbPagerMinLoadTimeLabel = vox::Renderer::CreateGUIText("Min Load Time (ms): ", m_valueTextSize);
        guiWindow.Add(pDbPagerMinLoadTimeLabel,
                      width - offsetLabelsWidth,
                      uiLineHeight + offsetUIElement);

        m_pDbPagerMinLoadTimeValueText = vox::Renderer::CreateGUIText("0", m_valueTextSize);
        guiWindow.Add(m_pDbPagerMinLoadTimeValueText,
                      width - offsetUIElementWidth + 45,
                      uiLineHeight + offsetUIElement);

        uiLineHeight += nextLineIncr;

        NvUIText* pDbPagerAvgLoadTimeLabel = vox::Renderer::CreateGUIText("Avg Load Time (ms): ", m_valueTextSize);
        guiWindow.Add(pDbPagerAvgLoadTimeLabel,
                      width - offsetLabelsWidth,
                      uiLineHeight + offsetUIElement);

        m_pDbPagerAvgLoadTimeValueText = vox::Renderer::CreateGUIText("0.0", m_valueTextSize);
        guiWindow.Add(m_pDbPagerAvgLoadTimeValueText,
                      width - offsetUIElementWidth + 45,
                      uiLineHeight + offsetUIElement);

        uiLineHeight += nextLineIncr;*/
    }

    uiLineHeight += nextLineIncr;
    
    m_pNodeListProcUILabel = vox::Renderer::CreateGUIText("Node List Threads:", m_valueTextSize);
    guiWindow.Add(m_pNodeListProcUILabel,
                  width - offsetLabelsWidth,
                  uiLineHeight);

    m_nodeListProcUILeft = width - (offsetLabelsWidth * 2.0f);

    //compute height now, but add to gui later
    uiLineHeight += nextLineIncr;
    m_nodeListProcUIStateBtnTop = uiLineHeight - 2.0f;

    size_t numNodeListProcs = GigaVoxelsOctTree::GetMaxNumNodeUsageListProcessors();
    m_nodeListProcUIElementIncr = (offsetLabelsWidth * 2.0f) / numNodeListProcs;

    float nodeProcUIHeight = valueBarHeight * 0.5f;
    float nodeProcUIMax = 10.0f;
    for(size_t i = 0;
        i < numNodeListProcs; 
        ++i)
    {
        m_nodeListProcUIStateBtns.push_back(std::make_pair(vox::Renderer::CreateGUIButton("IDLE",
                                                                                          stateBtnWidth,
                                                                                          stateBtnHeight,
                                                                                          2,
                                                                                          m_stateBtnTextSize),//num states
                                                           false));
    }

    uiLineHeight += nextLineIncr + 20.0f;

    m_pBrickUploadUILabel = vox::Renderer::CreateGUIText("Brick Uploads:", m_valueTextSize);
    guiWindow.Add(m_pBrickUploadUILabel, 
                  width - offsetLabelsWidth,
                  uiLineHeight);

    float uploadListMax = 1000.0f;
    m_pBrickUploadListValueBar = vox::Renderer::CreateGUIValueBar(valueBarWidth, valueBarHeight,
                                                                  uploadListMax, valueBarMin,
                                                                  valueBarInitValue);
    guiWindow.Add(m_pBrickUploadListValueBar, 
                  width - offsetUIElementWidth + 3.0f,
                  uiLineHeight + offsetUIElement);

    m_pBrickUploadListText = vox::Renderer::CreateGUIText("", m_valueTextSize);
    guiWindow.Add(m_pBrickUploadListText,
                  width - 3.0,
                  uiLineHeight);
}

template <class type> static void ToString(type from, std::string& toString)
{
    std::stringstream str;
    str.precision(1);
    str << std::fixed << from;
    str >> toString;
}

void GigaVoxelsRenderer::updateGUI(NvUIWindow& guiWindow)
{
    if(m_pDatabasePager != NULL)
    {
        DatabasePager::State state;
        size_t numPendingLoad = 0;
        size_t numLoaded = 0;;
        size_t maxLoadTimeMs = 0, minLoadTimeMs = 0;
        float avgLoadTimeMs = 0.0f;
        m_pDatabasePager->getStatus(state,
                                    numPendingLoad,
                                    numLoaded,
                                    maxLoadTimeMs,
                                    minLoadTimeMs,
                                    avgLoadTimeMs);
        
        unsigned int pagerUIDrawState = 0;
        std::string pagerState;
        switch(state)
        {
        case DatabasePager::INIT:
        case DatabasePager::WAITING:
        case DatabasePager::EXITING:
            pagerState = "IDLE";
            pagerUIDrawState = 0;
            break;
        case DatabasePager::LOADING:
            pagerState = "ACTIVE";
            pagerUIDrawState = 1;
            break;
        }

        m_pDbPagerStateBtn->SetTitle(pagerState.c_str(),
                                     m_stateBtnTextSize,
                                     true);
        m_pDbPagerStateBtn->SetDrawState(pagerUIDrawState);

        m_pDbPagerNumPendingLoadValueBar->SetValue(numPendingLoad);
        
        std::string strValue;
        ToString<size_t>(numPendingLoad, strValue);
        m_pDbPagerNumPendingLoadText->SetString(strValue.c_str());


        m_pDbPagerNumActiveValueBar->SetValue(numLoaded);

        ToString<size_t>(numLoaded, strValue);
        m_pDbPagerNumActiveText->SetString(strValue.c_str());

        //if(maxLoadTimeMs > 0)
        //{
        //    ToString<float>(maxLoadTimeMs, strValue);
        //    m_pDbPagerMaxLoadTimeValueText->SetString(strValue.c_str());
        //}

        //if(static_cast<long int>(minLoadTimeMs) > 0)
        //{
        //    ToString<float>(minLoadTimeMs, strValue);
        //    m_pDbPagerMinLoadTimeValueText->SetString(strValue.c_str());
        //}

        //if(avgLoadTimeMs > 0.0f)
        //{
        //    ToString<unsigned int>(avgLoadTimeMs, strValue);
        //    m_pDbPagerAvgLoadTimeValueText->SetString(strValue.c_str());
        //}

    }
    
    std::vector<bool> workerStates;
    std::vector<size_t> workerProcessListSizes;
    //add new node usage ui elements to window
    GigaVoxelsOctTree::GetNodeUsageListProcessorsStatus(workerStates,
                                                        workerProcessListSizes);
    for(size_t i = 0; i < workerStates.size(); ++i)
    {
        std::pair<NvUIButton*, bool>& btnItemPair = m_nodeListProcUIStateBtns[i];
        if(btnItemPair.second == false)
        {
            btnItemPair.second = true;
            guiWindow.Add(btnItemPair.first,
                          m_nodeListProcUILeft + (m_nodeListProcUIElementIncr * i),
                          m_nodeListProcUIStateBtnTop);
        }

        bool workerState = workerStates.at(i);
        if(workerState)
        {
            btnItemPair.first->SetTitle("ACTIVE", m_stateBtnTextSize, true);
            btnItemPair.first->SetDrawState(1);
        }
        else
        {
            btnItemPair.first->SetTitle("IDLE", m_stateBtnTextSize, true);
            btnItemPair.first->SetDrawState(0);
        }
    }
    
    size_t numBricksUploaded = BrickPool::instance().getNumBricksUploaded();
    m_pBrickUploadListValueBar->SetValue(numBricksUploaded);

    std::string strValue;
    ToString<size_t>(numBricksUploaded, strValue);
    m_pBrickUploadListText->SetString(strValue.c_str()); 
}

static void UpdateOctTrees(GigaVoxelsRenderer::Drawable* pUpdateList)
{
    try
    {
        for(GigaVoxelsRenderer::Drawable* pDrawable = pUpdateList;
            pDrawable != nullptr;
            pDrawable = pDrawable->pNext)
        {
            vox::SmartPtr<GigaVoxelsOctTree> spSVO = pDrawable->spOctTree.get();
            spSVO->update();
        }
    }
    catch(std::bad_alloc& ba)
    {
        std::cerr << "Bad Alloc caught." << ba.what() << std::endl;
    }
}

void GigaVoxelsRenderer::update(vox::Camera& camera,
                                vox::SceneObject& scene)
{
    QElapsedTimer updateTimer;
    updateTimer.start();

    if(m_pDatabasePager != nullptr)
    {
        try
        {
            m_pDatabasePager->getBrickUploadRequests(BrickPool::instance().getUploadRequestList());
        }
        catch(std::bad_alloc& ba)
        {
            std::cerr << "Bad Alloc caught." << ba.what() << std::endl;
        }
    }

    qint64 get = updateTimer.restart();

    try
    {
        GigaVoxelsOctTree::UpdateBrickPool();
    }
    catch(std::bad_alloc& ba)
    {
        std::cerr << "Bad Alloc caught." << ba.what() << std::endl;
    }

    qint64 bricks = updateTimer.restart();

    if(m_pDatabasePager != nullptr)
    {
        try
        {
            m_pDatabasePager->mergeHandledRequests(&m_debugRenderer);
            m_pDatabasePager->unloadStalePagedOctTreeNodes(camera.getFrameCount(), &m_debugRenderer);
        }
        catch(std::bad_alloc& ba)
        {
            std::cerr << "Bad Alloc caught." << ba.what() << std::endl;
        }
    }

    m_pUpdateList = m_pRenderList;

    m_pRenderList = nullptr;

    if(m_pUpdateList != nullptr)
    {
        UpdateOctTrees(m_pUpdateList);
    }

    qint64 svo = updateTimer.elapsed();
    qint64 updateTimeMs = svo + bricks + get;
    uint32_t prevUpdateTimeMs;
    m_pUpdateTimeUI->GetValue(prevUpdateTimeMs);
    updateTimeMs += prevUpdateTimeMs;
    updateTimeMs >>= 1;
    m_pUpdateTimeUI->SetValue(static_cast<uint32_t>(updateTimeMs));
    if(svo > bricks)
        std::cout << "UPDATE TIME = " << updateTimeMs << " get=" << get << " bricks=" << bricks << " svo=" << svo << std::endl;
}

void GigaVoxelsRenderer::drawVolume(vox::Camera& camera,
                                    vox::SceneObject& voxels,
                                    GigaVoxelsOctTree& octTree,
                                    size_t numSamples,
                                    bool cameraInsideVolume)
{
    m_spFrameBufferObject->mapDrawBuffers(4);

    voxOpenGL::GLUtils::CheckOpenGLError();

    //attach color texture to color attach point 0
    m_spFrameBufferObject->attachColorBuffer(m_colorTextureID, 0);
    //attach 3 layers of node usage texture to color attach point 1
    m_spFrameBufferObject->attach2DArrayLayerColorBuffer(octTree.getNodeUsageTextureID(), 
                                                        1,
                                                        0, 3);
    m_spFrameBufferObject->attachDepthStencilBuffer();

    glEnable(GL_STENCIL_TEST);
    //use the stencil to make sure that only one pixel modifies the color buffer
    glStencilFunc(GL_EQUAL, 0, 0xFFFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);

    //static GLfloat zeroFloat[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    //glClearBufferfv(GL_COLOR, 0, zeroFloat);

    //clear the node usage textures
    static GLuint zeroUint[] = { 0, 0, 0, 0 };
    glClearBufferuiv(GL_COLOR, 1, zeroUint);
    glClearBufferuiv(GL_COLOR, 2, zeroUint);
    glClearBufferuiv(GL_COLOR, 3, zeroUint);

    //static GLfloat depth = 1.0f;
    static GLint stencil = 0;
    //glClearBufferfi(GL_DEPTH_STENCIL, 0, depth, stencil);
    glClearBufferiv(GL_STENCIL, 0, &stencil);
    
    m_pDrawVolumeShader->bind();

    int debugMode = getDrawDebug();
    if(debugMode == 2 || debugMode == 3 || debugMode == 4)
    {
        m_pDrawVolumeShader->setUniformValue("ShowConstantNodes",
                                             debugMode == 3 || debugMode == 4);
    }

    bool brickGradientsAreUnsigned = octTree.getBrickGradientsAreUnsigned();
    if(getEnableLighting())
        m_pDrawVolumeShader->setUniformValue("GradientsAreUnsigned", brickGradientsAreUnsigned);

    int octTreeDepthMinusOne = static_cast<int>(octTree.getDepth()) - 1;
    float leafNodeSize = 1.0f / std::pow(2.0f, static_cast<float>(octTreeDepthMinusOne));
    int nodeCountWidth = static_cast<size_t>(1.0f / leafNodeSize);
    int nodeCountWidth2 = (nodeCountWidth * nodeCountWidth);
    int maxCount = static_cast<int>(std::sqrt(static_cast<float>(nodeCountWidth2 + nodeCountWidth2 + nodeCountWidth2)));
    m_pDrawVolumeShader->setUniformValue("MaxTreeTraversals", maxCount * 2);
    bool rootNodeIsConstant = 
            octTree.getRootNode()->getNodeTypeFlag() == GigaVoxelsOctTree::Node::CONSTANT_NODE;

    m_pDrawVolumeShader->setUniformValue("RootNodeIsConstant", rootNodeIsConstant);

    const QMatrix4x4& projMtx = camera.getProjectionMatrix();
    //glGetDoublev(GL_PROJECTION_MATRIX, projMtx.data());
    
    m_pDrawVolumeShader->setUniformValue("ProjectionMatrix", projMtx);
    
    const QMatrix4x4& modelViewMtx = camera.getViewMatrix();
    //glGetDoublev(GL_MODELVIEW_MATRIX, modelViewMtx.data());
    
    m_pDrawVolumeShader->setUniformValue("ModelViewMatrix", modelViewMtx);
    
    const vox::BoundingBox& bbox = voxels.getBoundingBox();

    //translation
    QVector3D volTranslation = bbox.center();
    m_pDrawVolumeShader->setUniformValue("VolTranslation",
                                   volTranslation.x(),
                                   volTranslation.y(),
                                   volTranslation.z());
    
    //scale
    QVector3D volScale = QVector3D(bbox.xMax() - bbox.xMin(), 
                                   bbox.yMax() - bbox.yMin(), 
                                   bbox.zMax() - bbox.zMin());

    m_pDrawVolumeShader->setUniformValue("VolScale", 
                                   volScale.x(),
                                   volScale.y(),
                                   volScale.z());
    
    QVector3D volExtentMin = bbox.minimum() - volTranslation;
    volExtentMin.setX((volExtentMin.x() / volScale.x()) + 0.5f);
    volExtentMin.setY((volExtentMin.y() / volScale.y()) + 0.5f);
    volExtentMin.setZ((volExtentMin.z() / volScale.z()) + 0.5f);
    m_pDrawVolumeShader->setUniformValue("VolExtentMin", volExtentMin);
    
    QVector3D volExtentMax = bbox.maximum() - volTranslation;
    volExtentMax.setX((volExtentMax.x() / volScale.x()) + 0.5f);
    volExtentMax.setY((volExtentMax.y() / volScale.y()) + 0.5f);
    volExtentMax.setZ((volExtentMax.z() / volScale.z()) + 0.5f);
    m_pDrawVolumeShader->setUniformValue("VolExtentMax", volExtentMax);
    
    QVector3D cameraPosition = camera.getPosition() - volTranslation;
    cameraPosition.setX((cameraPosition.x() / volScale.x()) + 0.5f);
    cameraPosition.setY((cameraPosition.y() / volScale.y()) + 0.5f);
    cameraPosition.setZ((cameraPosition.z() / volScale.z()) + 0.5f);
    m_pDrawVolumeShader->setUniformValue("CameraPosition", cameraPosition);

    m_pDrawVolumeShader->setUniformValue("CameraUp", camera.getUp());
    m_pDrawVolumeShader->setUniformValue("CameraLeft", camera.getLeft());

    QVector3D lightPosition(0.0f, 100.0f, 0.0f);
    lightPosition.setX((lightPosition.x() / volScale.x()) + 0.5f);
    lightPosition.setY((lightPosition.y() / volScale.y()) + 0.5f);
    lightPosition.setZ((lightPosition.z() / volScale.z()) + 0.5f);
    m_pDrawVolumeShader->setUniformValue("LightPosition", lightPosition);

	bool computeLighting = getComputeLighting();
    if(getEnableLighting())
	    m_pDrawVolumeShader->setUniformValue("ComputeLighting", computeLighting);
    
    float rayStepSize = octTree.getRayStepSize() * 0.5f;
    if((rayStepSize > 0.0f) == false)
    //{
    //    float pow2 = std::pow(2.0f, static_cast<float>(pSVO->getDepth()-1));
    //    brickStepSize = rayStepSize * pow2;
    //}
    //else
    {
        float sqrt3 = std::sqrt(3.0f);
        rayStepSize = (sqrt3 / numSamples);
        //float pow2 = std::pow(2.0f, static_cast<float>(pSVO->getDepth()-1));
        //float numSamplesRootBrick = numSamples / pow2;
        //brickStepSize = sqrt3 / numSamplesRootBrick;
    }
    
    m_pDrawVolumeShader->setUniformValue("RayStepSize", 
                                   static_cast<float>(rayStepSize));

    //m_pDrawVolumeShader->setUniformValue("BrickStepSize", 
    //                                     brickStepSize);
    //m_pDrawVolumeShader->setUniformValue("ParentBrickStepSize", 
    //                                     brickStepSize*0.5f);
    
    QVector3D rayStepRootBrick = volScale;
    rayStepRootBrick *= rayStepSize;
    float pow2 = std::pow(2.0f, static_cast<float>(octTree.getDepth()-1));
    rayStepRootBrick *= pow2;

    QVector3D brickDim = octTree.getBrickDimension();
    QVector3D rootVoxelSize(volScale.x() / brickDim.x(), 
                            volScale.y() / brickDim.y(), 
                            volScale.z() / brickDim.z());
    QVector3D rayStepToVoxelSizeRatio(rayStepRootBrick.x() / rootVoxelSize.x(),
                                      rayStepRootBrick.y() / rootVoxelSize.y(),
                                      rayStepRootBrick.z() / rootVoxelSize.z());
    
    QVector3D brickPoolDimension = octTree.getBrickPoolDimension();
    QVector3D brickPoolVoxelSize(1.0f / brickPoolDimension.x(),
                                 1.0f / brickPoolDimension.y(),
                                 1.0f / brickPoolDimension.z());

    QVector3D brickStepVector = brickPoolVoxelSize;
    brickStepVector *= rayStepToVoxelSizeRatio;
    //LeafBrickStepSizeTransform converts from normalized volume/voxel coords to brick pool coords
    //by first transforming to world coords and then to brick pool coords
    m_pDrawVolumeShader->setUniformValue("BrickStepVector",
                                         brickStepVector);
    m_pDrawVolumeShader->setUniformValue("OctTreeDepthMinusOne",
                                         octTreeDepthMinusOne);
    
    //QVector3D pointOnNearPlane = (camera.getPosition() 
    //                              + (camera.getLook() * camera.getNearPlaneDist()))
    //                              - volTranslation;

    //pointOnNearPlane.setX((pointOnNearPlane.x() / volScale.x()) + 0.5f);
    //pointOnNearPlane.setY((pointOnNearPlane.y() / volScale.y()) + 0.5f);
    //pointOnNearPlane.setZ((pointOnNearPlane.z() / volScale.z()) + 0.5f);

    //vox::Plane nearPlane(pointOnNearPlane, 
    //                     camera.getLook());

    //m_pDrawVolumeShader->setUniformValue("NearPlane", 
    //                               (GLfloat)nearPlane.normal().x(),
    //                               (GLfloat)nearPlane.normal().y(),
    //                               (GLfloat)nearPlane.normal().z(),
    //                               (GLfloat)nearPlane.planeD());
    //
    //float distToNearPlane = std::abs(nearPlane.eval(cameraPosition));

    //m_pDrawVolumeShader->setUniformValue("NearPlaneDistance", 
    //                                     distToNearPlane);
    
    m_pDrawVolumeShader->setUniformValue("BrickDimension",
                                         brickDim);

    float minBrickDim = brickDim.x() < 
                         ((brickDim.y() < brickDim.z()) ? brickDim.y() : 
                                                         brickDim.z()) ? 
                        brickDim.x() : brickDim.z();

    //m_pDrawVolumeShader->setUniformValue("MinBrickDimension", minBrickDim);
    float rootVoxelHalfSize = (1.0f / minBrickDim) * 0.5f;//half size of voxel in root node
    m_pDrawVolumeShader->setUniformValue("RootVoxelHalfSize", rootVoxelHalfSize);

    m_pDrawVolumeShader->setUniformValue("BrickPoolDimension",
                                         brickPoolDimension);
    
    //float maxScale = std::max(std::max(volScale.x(), volScale.y()), volScale.z());
    float pixelSize = camera.getPixelSize();// / maxScale;

    /*int width, height;
    camera.getViewportWidthHeight(width, height);
    float nearPlaneWidth, nearPlaneHeight;
    camera.getNearPlaneWidthHeight(nearPlaneWidth, nearPlaneHeight);
    float nearPlaneSize = (nearPlaneWidth < nearPlaneHeight ? nearPlaneWidth : nearPlaneHeight) 
                            / maxScale;
    pixelSize = (nearPlaneSize / std::max(width, height)) * m_lodScalar;*/

    m_pDrawVolumeShader->setUniformValue("PixelSize", pixelSize);

	float lodScalar = getLodScalar();
	m_pDrawVolumeShader->setUniformValue("LodScalar", lodScalar);

    //a pixel can record 12 nodes in the node usage list
    //in odd frames we'll record the first 12
    //in even frames we'll record the next 12
    //a 2x2 block of pixels records a total of 48 
    //(by sliding the window we can record up to 96 nodes)
    int maxNodesPerPixel = 12;
    int maxNodesToPush = static_cast<int>(maxNodesPerPixel 
                                          * ((camera.getFrameCount() % 2)+1));
    m_pDrawVolumeShader->setUniformValue("MaxNodesToPushThisFrame", maxNodesToPush);

    //vox::TextureIDArray& textureIDs = voxels.getTextureIDArray();
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_3D, octTree.getTreeTextureID());
    
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, octTree.getBrickTextureID());

    if(getEnableLighting())
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_3D, octTree.getBrickGradientTextureID());

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_jitterTextureID);
    }
    else
    {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_jitterTextureID);
    }
        
    //enable depth test so that any non-gigavoxel objects will prevent rasterization
    //glEnable(GL_DEPTH_TEST);
    //glDisable(GL_DEPTH_TEST);

    glEnable(GL_CULL_FACE);
    bool runTest = false;
    if(runTest)
    {
        vox::FloatArray& vertexArray = voxels.getFloatVertexArray();

        GigaVoxelsShaderCodeTester::drawVolume(m_pDrawVolumeShader, 
                                               &octTree,
                                               vertexArray,
                                               camera.getLook().x(),
                                               camera.getLook().y(),
                                               camera.getLook().z());
    }

    voxels.draw();

    m_pDrawVolumeShader->release();

    if(cameraInsideVolume)
    {
        m_pCameraNearPlaneShader->bind();

        if(getEnableLighting())
            m_pCameraNearPlaneShader->setUniformValue("GradientsAreUnsigned", brickGradientsAreUnsigned);
        m_pCameraNearPlaneShader->setUniformValue("MaxTreeTraversals", maxCount * 2);
        m_pCameraNearPlaneShader->setUniformValue("RootNodeIsConstant", rootNodeIsConstant);

        m_pCameraNearPlaneShader->setUniformValue("ProjectionMatrix", projMtx);
        m_pCameraNearPlaneShader->setUniformValue("ModelViewMatrix", modelViewMtx);
        m_pCameraNearPlaneShader->setUniformValue("InverseModelViewMatrix", modelViewMtx.inverted());
        m_pCameraNearPlaneShader->setUniformValue("RayStepSize", rayStepSize);
        m_pCameraNearPlaneShader->setUniformValue("VolTranslation",
                                                volTranslation.x(),
                                                volTranslation.y(),
                                                volTranslation.z());
        m_pCameraNearPlaneShader->setUniformValue("VolScale", 
                                                volScale.x(),
                                                volScale.y(),
                                                volScale.z());
        m_pCameraNearPlaneShader->setUniformValue("VolExtentMin", volExtentMin);
        m_pCameraNearPlaneShader->setUniformValue("VolExtentMax", volExtentMax);
        m_pCameraNearPlaneShader->setUniformValue("CameraPosition", cameraPosition);
        m_pCameraNearPlaneShader->setUniformValue("CameraUp", camera.getUp());
        m_pCameraNearPlaneShader->setUniformValue("CameraLeft", camera.getLeft());
        m_pCameraNearPlaneShader->setUniformValue("LightPosition", lightPosition);
		if(getEnableLighting())
            m_pCameraNearPlaneShader->setUniformValue("ComputeLighting", computeLighting);

        //m_pCameraNearPlaneShader->setUniformValue("BrickStepSize", 
        //                                     brickStepSize);
        //m_pCameraNearPlaneShader->setUniformValue("ParentBrickStepSize", 
        //                                     brickStepSize*0.5f);
        m_pCameraNearPlaneShader->setUniformValue("BrickStepVector",
                                                  brickStepVector);
        m_pCameraNearPlaneShader->setUniformValue("OctTreeDepthMinusOne",
                                                  octTreeDepthMinusOne);
    
        //m_pCameraNearPlaneShader->setUniformValue("NearPlane", 
        //                               (GLfloat)nearPlane.normal().x(),
        //                               (GLfloat)nearPlane.normal().y(),
        //                               (GLfloat)nearPlane.normal().z(),
        //                               (GLfloat)nearPlane.planeD());
    
        //m_pCameraNearPlaneShader->setUniformValue("NearPlaneDistance", 
        //                               distToNearPlane);
    
        m_pCameraNearPlaneShader->setUniformValue("BrickDimension",
                                                  brickDim);

        //m_pCameraNearPlaneShader->setUniformValue("MinBrickDimension", 
                                                  //minBrickDim);
        m_pCameraNearPlaneShader->setUniformValue("RootVoxelHalfSize",
                                                  rootVoxelHalfSize);

        m_pCameraNearPlaneShader->setUniformValue("BrickPoolDimension",
                                        octTree.getBrickPoolDimension());
    
        m_pCameraNearPlaneShader->setUniformValue("PixelSize",
                                                  pixelSize);

		m_pCameraNearPlaneShader->setUniformValue("LodScalar", lodScalar);

        m_pCameraNearPlaneShader->setUniformValue("MaxNodesToPushThisFrame",
                                                  maxNodesToPush);

        voxOpenGL::GLUtils::DrawNearPlaneQuad();

        m_pCameraNearPlaneShader->release();
    }
    
    glDisable(GL_STENCIL_TEST);
}

void GigaVoxelsRenderer::generateNodeUsageSelectionMask(GigaVoxelsOctTree& octTree,
                                                        const vox::Camera& camera)
{
    m_spFrameBufferObject->attachColorBuffer(octTree.getSelectionMaskTextureID(), 0);

    m_spFrameBufferObject->detachDepthStencilBuffer();

    s_pGenerateSelectionMaskShader->bind();

    int width, height;
    camera.getViewportWidthHeight(width, height);
    
    s_pGenerateSelectionMaskShader->setUniformValue("NodeUsageListDimMinusTwo", width-2, height-2);

    //bind node usage textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, octTree.getNodeUsageTextureID());

    /*bool runTest = false;
    if(runTest)
    {
        GigaVoxelsShaderCodeTester::generateNodeUsageSelectionMask(s_pGenerateSelectionMaskShader,
                                                                   nodeUsageTextureID);
    }*/

    voxOpenGL::GLUtils::DrawScreenAlignedQuad();

    s_pGenerateSelectionMaskShader->release();
}

void GigaVoxelsRenderer::generateSelectionMaskHistoPyramid(GigaVoxelsOctTree& octTree, 
                                                           vox::Camera& camera)
{
    int width, height;
    camera.getViewportWidthHeight(width, height);

    //attach level 0 of the histo pyramid texture
    int histoPyramidRenderLevel = 0;
    unsigned int histoPyramidTextureID = octTree.getHistoPyramidTextureID();
    m_spFrameBufferObject->attachColorBuffer(histoPyramidTextureID, 
                                             0, histoPyramidRenderLevel);
    
    //first pass is to generate the active cell texture
    s_pComputeActiveTexelsShader->bind();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, octTree.getSelectionMaskTextureID());
    while(true)
    {
        voxOpenGL::GLUtils::CheckOpenGLError();
        /*bool runTest = false;
        if(runTest)
        {
            if(histoPyramidRenderLevel == 0)
                GigaVoxelsShaderCodeTester::computeActiveTexels(m_selectionMaskTextureID);
            else
            {
                
                GigaVoxelsShaderCodeTester::generateSelectionMaskHistoPyramid(width, height,
                                                                              histoPyramidTextureID,
                                                                              histoPyramidRenderLevel-1);
            }
        }*/

        voxOpenGL::GLUtils::DrawScreenAlignedQuad();

        if(width == 1 && height == 1)
            break;

        //shrink the viewport
        if(width != 1)
            width >>= 1;
        if(height != 1)
            height >>= 1;

        m_spFrameBufferObject->setViewportWidthHeight(width, height);

        if(histoPyramidRenderLevel == 0)
        {
            s_pComputeActiveTexelsShader->release();//release the active texels shader
            
            s_pGenerateHistoPyramidShader->bind();
            //now read from histopyramid texture
            glBindTexture(GL_TEXTURE_2D, histoPyramidTextureID);
        }

        //set the read texture so that there is only one mipmap level
        //we read from the previous level to compute the next level
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, histoPyramidRenderLevel);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, histoPyramidRenderLevel);

        //render to next level in texture
        ++histoPyramidRenderLevel;
        int attachIndex = 0;
        m_spFrameBufferObject->attachColorBuffer(histoPyramidTextureID,
                                                 attachIndex, 
                                                 histoPyramidRenderLevel);
    }

    s_pGenerateHistoPyramidShader->release();

    //restore texture mip map settings
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);

    octTree.asyncDownloadCompressedNodeUsageListLength(histoPyramidRenderLevel);

    m_spFrameBufferObject->setViewportWidthHeight(camera.getViewportWidth(),
                                                  camera.getViewportHeight());
}

void GigaVoxelsRenderer::compressNodeUsageList(GigaVoxelsOctTree& octTree, 
                                               vox::Camera& camera)
{
    //generate a histopyramid for the selection mask
    //the sum computed in the 1x1 mip map level of the histopyramid
    //is the compressed node lists length
    GigaVoxelsOctTree::NodeUsageListParams& nodeUsageList = octTree.getWriteCompressedNodeUsageList();

    s_timer.startTimer("getCompressedNodeUsageList");
    unsigned int histoPyramidTextureID;
    nodeUsageList.listLength = octTree.getDownloadedCompressedNodeUsageListLength(histoPyramidTextureID);
    s_timer.stopTimer("getCompressedNodeUsageList");

    if(nodeUsageList.listLength > 0)
    {
        if(nodeUsageList.listLength > 32768)
            nodeUsageList.listLength = 32768;

        s_timer.startTimer("compressNodeUsageList");
        //std::cout << nodeUsageList.listLength << std::endl;
        s_pCompressNodeUsageListShader->bind();

        s_pCompressNodeUsageListShader->setUniformUIntValue("CompressedSelectionMaskMaxIndex",
                                                            nodeUsageList.listLength - 1);

        //glActiveTexture(GL_TEXTURE0);
        int outTextureWidth = (int)std::ceil(std::sqrt((float)nodeUsageList.listLength));
        int outTextureHeight = (int)std::ceil((float)nodeUsageList.listLength / (float)outTextureWidth);
        if(nodeUsageList.width != outTextureWidth
            || nodeUsageList.height != outTextureHeight)
        {
            glBindTexture(GL_TEXTURE_2D, nodeUsageList.textureID);
        
            nodeUsageList.width = outTextureWidth;
            nodeUsageList.height = outTextureHeight;

            //resize the texture
            glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGBA32UI,
                            nodeUsageList.width,
                            nodeUsageList.height,
                            0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL);

            s_pCompressNodeUsageListShader->setUniformValue("CompressedSelectionMaskWidth", 
                                                            nodeUsageList.width);
        }

        //there are potentially 12 items in the list
        //and we can hold 4 in the output so use a FIFO
        //to grab extra and push different amount depending
        //on frame number
        int maxNodesPerPixel = 4;
        int maxNodesToPush = static_cast<int>(maxNodesPerPixel 
                                                * ((camera.getFrameCount() % 3)+1));
        s_pCompressNodeUsageListShader->setUniformValue("MaxNodesToPushThisFrame", maxNodesToPush);

        /*bool runTest = false;
        if(runTest)
        {
            GigaVoxelsShaderCodeTester::compressNodeUsageList(s_pCompressNodeUsageListShader,
                                                                nodeUsageList.width,
                                                                nodeUsageList.height,
                                                                histoPyramidTextureID,
                                                                octTree.getNodeUsageTextureID(),
                                                                m_selectionMaskTextureID);
        }*/

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 
                      histoPyramidTextureID);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, octTree.getNodeUsageTextureID());

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, octTree.getSelectionMaskTextureID());

        m_spFrameBufferObject->attachColorBuffer(nodeUsageList.textureID, 0);

        m_spFrameBufferObject->setViewportWidthHeight(nodeUsageList.width,
                                                      nodeUsageList.height);

        voxOpenGL::GLUtils::DrawScreenAlignedQuad();

        s_pCompressNodeUsageListShader->release();

        s_timer.stopTimer("compressNodeUsageList");
    }
}