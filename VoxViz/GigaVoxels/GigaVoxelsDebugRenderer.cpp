#include "GigaVoxels/GigaVoxelsDebugRenderer.h"

#include "VoxVizCore/Camera.h"
#include "VoxVizOpenGL/GLUtils.h"

#include <QtGui/qvector3d.h>

#include <list>

using namespace gv;

static voxOpenGL::ShaderProgram* s_pShader = NULL;

GigaVoxelsDebugRenderer::GigaVoxelsDebugRenderer() :
    m_initialized(false),
    m_viewPortX(700),
    m_viewPortY(520),
    m_viewPortWidth(100),
    m_viewPortHeight(80),
    m_colorTextureID(0),
    m_frustumVAO(0),
    m_frustumVBO(0),
    m_frustumEBO(0),
    m_pagedNodeVAO(0),
    m_pagedNodeVBO(0),
    m_pagedNodeEBO(0)
{
}

GigaVoxelsDebugRenderer::~GigaVoxelsDebugRenderer()
{
    //delete frustum resources
    if(m_colorTextureID != 0)
        glDeleteTextures(1, &m_colorTextureID);
    if(m_frustumVBO != 0)
        glDeleteBuffers(1, &m_frustumVBO);
    if(m_frustumVAO)
        glDeleteVertexArrays(1, &m_frustumVAO);
    if(m_frustumEBO != 0)
        glDeleteBuffers(1, &m_frustumEBO);

    //delete paged node resources
    if(m_pagedNodeVAO != 0)
        glDeleteVertexArrays(1, &m_pagedNodeVAO);
    if(m_pagedNodeVBO != 0)
        glDeleteBuffers(1, &m_pagedNodeVBO);
    if(m_pagedNodeEBO != 0)
        glDeleteBuffers(1, &m_frustumEBO);
}

static void CreateFrustumDrawable(float fovY, float aspectRatio, float nearPlane, float farPlane,
                                  GLuint& vao,
                                  GLuint& vbo,
                                  GLuint& ebo)
{
    const double DEG2RAD = 3.14159265f / 180.0f;
    float tangent = tanf(fovY/2 * DEG2RAD);
    float nearHeight = nearPlane * tangent;
    float nearWidth = nearHeight * aspectRatio;
    float farHeight = farPlane * tangent;
    float farWidth = farHeight * aspectRatio;

    voxOpenGL::GLUtils::VertexArray vertices;
    // compute 8 vertices of the frustum
    vertices.resize(8);
    // near top right
    vertices[0] = vox::Vec3f(nearWidth, nearHeight, -nearPlane);
    // near top left
    vertices[1] = vox::Vec3f(-nearWidth, nearHeight, -nearPlane);
    // near bottom left
    vertices[2] = vox::Vec3f(-nearWidth, -nearHeight, -nearPlane);
    // near bottom right
    vertices[3] = vox::Vec3f(nearWidth, -nearHeight, -nearPlane);
    // far top right
    vertices[4] = vox::Vec3f(farWidth, farHeight, -farPlane);
    // far top left
    vertices[5] = vox::Vec3f(-farWidth, farHeight, -farPlane);
    // far bottom left
    vertices[6] = vox::Vec3f(-farWidth, -farHeight, -farPlane);
    // far bottom right
    vertices[7] = vox::Vec3f(farWidth, -farHeight, -farPlane);

    voxOpenGL::GLUtils::CreateVertexBuffer(vertices, vao, vbo);

    //create a indexed draw elements to draw theses vertices as a list
    int frustumIndices[] = {
        //near plane triangles
        0, 1, 2,
        2, 3, 0,
        //far plane triangles
        4, 7, 6,
        6, 5, 4,
        //right plane triangles
        0, 2, 7,
        7, 4, 0,
        //left plane triangles
        1, 5, 6,
        6, 2, 1,
        //top plane triangles
        0, 4, 5,
        5, 1, 0,
        //bottom plane triangles
        3, 2, 6,
        6, 7, 3
    };

    voxOpenGL::GLUtils::IntArray indices(&frustumIndices[0], &frustumIndices[36]);
    voxOpenGL::GLUtils::CreateElementArrayBuffer(indices, ebo);
}

class PagedNodeCollector : public gv::NodeVisitor
{
private:
    GigaVoxelsDebugRenderer::PagedNodes& m_pagedNodes;
public:
    PagedNodeCollector(GigaVoxelsDebugRenderer::PagedNodes& pagedNodes) : m_pagedNodes(pagedNodes) {}

    virtual void apply(PagedOctTreeNode& node) override
    {
        m_pagedNodes[&node] = GigaVoxelsDebugRenderer::PagedNodeDrawable(node);
        traverse(node);
    }
};

static void CreatePagedNodeDrawables(GigaVoxelsDebugRenderer::PagedNodes& pagedNodeDrawables,
                                     GLuint& pagedNodeVAO,
                                     GLuint& pagedNodeVBO,
                                     GLuint& pagedNodeEBO)
{
    //create vao, vbo for drawing paged nodes
    voxOpenGL::GLUtils::VertexArray vertices;
    //quad centered on origin, will translate into position
    vertices.push_back(vox::Vec3f(-0.5f, -0.5f, 0.0f));
    vertices.push_back(vox::Vec3f(0.5f, -0.5f, 0.0f));
    vertices.push_back(vox::Vec3f(0.5f, 0.5f, 0.0f));
    vertices.push_back(vox::Vec3f(-0.5f, 0.5f, 0.0f));
    voxOpenGL::GLUtils::CreateVertexBuffer(vertices, pagedNodeVAO, pagedNodeVBO);
    //create ebo for drawing paged nodes
    voxOpenGL::GLUtils::IntArray indices;
    indices.push_back(0); indices.push_back(1); indices.push_back(2);
    indices.push_back(2), indices.push_back(3); indices.push_back(0);
    voxOpenGL::GLUtils::CreateElementArrayBuffer(indices, pagedNodeEBO);
}

void GigaVoxelsDebugRenderer::init(vox::Camera& camera,
                                   Node& sceneRoot,
                                   voxOpenGL::ShaderProgram* pShader)
{
    if(m_initialized)
        return;

    m_initialized = true;

    CreateFrustumDrawable(camera.getFieldOfView(),
                          camera.getAspectRatio(),
                          camera.getNearPlaneDist(),
                          camera.getFarPlaneDist(),
                          m_frustumVAO,
                          m_frustumVBO,
                          m_frustumEBO);
    
    //traverse tree and collect paged nodes
    PagedNodeCollector pagedNodeCollector(m_pagedNodeDrawables);
    sceneRoot.accept(pagedNodeCollector);

    CreatePagedNodeDrawables(m_pagedNodeDrawables,
                             m_pagedNodeVAO,
                             m_pagedNodeVBO,
                             m_pagedNodeEBO);

    //create the textures for coloring active, loading, inactive, and frustum
    const size_t numColors = 5;
    vox::Vec4ub colorLookUpTable[numColors] = 
    {
        vox::Vec4ub(0, 255, 0, 128), //green for active nodes
        vox::Vec4ub(255, 255, 0, 128), //yellow for loading nodes,
        vox::Vec4ub(255, 0, 0, 128), //red for inactive nodes
        vox::Vec4ub(255, 255, 255, 128), //transparent white for edges of nodes
        vox::Vec4ub(0, 0, 0, 128) //black for frustum
    };

    m_colorTextureID = voxOpenGL::GLUtils::Create1DTexture((unsigned char*)colorLookUpTable, numColors);

    s_pShader = pShader;
    if(s_pShader && s_pShader->bind())
    {
        s_pShader->setUniformValue("ColorLookupSampler", 0);
        s_pShader->release();
    }
}

void GigaVoxelsDebugRenderer::notifyPagedNodeIsActive(gv::PagedOctTreeNode& node)
{
    m_pagedNodeDrawables[&node].m_state = GigaVoxelsDebugRenderer::PAGED_NODE_ACTIVE;
}

void GigaVoxelsDebugRenderer::notifyPagedNodeIsLoading(gv::PagedOctTreeNode& node)
{
    m_pagedNodeDrawables[&node].m_state = GigaVoxelsDebugRenderer::PAGED_NODE_LOADING;
}

void GigaVoxelsDebugRenderer::notifyPagedNodeIsInactive(gv::PagedOctTreeNode& node)
{
    m_pagedNodeDrawables[&node].m_state = GigaVoxelsDebugRenderer::PAGED_NODE_INACTIVE;
}

static void DrawPagedNodes(const std::list<PagedOctTreeNode*>& nodeList)
{
    for(std::list<PagedOctTreeNode*>::const_iterator itr = nodeList.begin();
        itr != nodeList.end();
        ++itr)
    {
        PagedOctTreeNode* pCurNode = *itr;

        const QVector3D& translation = pCurNode->center();
        const QVector3D& scale = pCurNode->getOctTreeSizeMeters();
        QMatrix4x4 worldMtx;
        worldMtx.translate(translation);
        worldMtx.scale(scale);

        s_pShader->setUniformValue("WorldTransform", worldMtx);

        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, (GLvoid*)0);
    }
}

static void BuildDrawListsAndDrawOutlines(const GigaVoxelsDebugRenderer::PagedNodes& pagedNodeDrawables,
                           std::list<PagedOctTreeNode*>& activeList,
                           std::list<PagedOctTreeNode*>& inactiveList,
                           std::list<PagedOctTreeNode*>& loadingList)
{
    for(GigaVoxelsDebugRenderer::PagedNodes::const_iterator itr = pagedNodeDrawables.begin();
        itr != pagedNodeDrawables.end();
        ++itr)
    {
        switch(itr->second.m_state)
        {
        case GigaVoxelsDebugRenderer::PAGED_NODE_ACTIVE:
            {
                activeList.push_back(itr->first);
                break;
            }
        case GigaVoxelsDebugRenderer::PAGED_NODE_LOADING:
            {
                loadingList.push_back(itr->first);
                break;
            }
        case GigaVoxelsDebugRenderer::PAGED_NODE_INACTIVE:
            {
                inactiveList.push_back(itr->first);
                break;
            }
        }
        PagedOctTreeNode* pCurNode = itr->first;

        const QVector3D& translation = pCurNode->center();
        const QVector3D& scale = pCurNode->getOctTreeSizeMeters();
        QMatrix4x4 worldMtx;
        worldMtx.translate(translation);
        worldMtx.scale(scale);

        s_pShader->setUniformValue("WorldTransform", worldMtx);

        glDrawArrays(GL_LINE_LOOP, 0, 4);
    }
}

void GigaVoxelsDebugRenderer::draw(vox::Camera& camera)
{
    if(m_initialized == false)
        return;

    int width, height;
    camera.getViewportWidthHeight(width, height);

    m_viewPortWidth = std::ceil(static_cast<float>(width) * 0.2f);
    m_viewPortHeight = std::ceil(static_cast<float>(height) * 0.2f);
    m_viewPortX = width - m_viewPortWidth - 5;
    m_viewPortY = height - m_viewPortHeight - 5;

    glViewport(m_viewPortX, m_viewPortY, m_viewPortWidth, m_viewPortHeight);
    glEnable(GL_SCISSOR_TEST);
    glScissor(m_viewPortX, m_viewPortY, m_viewPortWidth, m_viewPortHeight);

    static GLfloat zeroFloat[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    glClearBufferfv(GL_COLOR, 0, zeroFloat);

    static GLfloat depth = 1.0f;
    glClearBufferfv(GL_DEPTH, 0, &depth);

    glEnable(GL_DEPTH_TEST);
    
    vox::FlyCamera topDownCamera;
    QVector3D topDownCamLookAt = camera.getPosition();
    topDownCamera.setLookAt(topDownCamLookAt);

    QVector3D topDownCamUp(0.0, 0.0, 1.0);
    QVector3D topDownCamPos = topDownCamLookAt + (topDownCamUp * 450.0);
    topDownCamera.setPosition(topDownCamPos);

    QVector3D worldYAxis(0.0, 1.0, 0.0);
    topDownCamera.setWorldUp(worldYAxis);

    s_pShader->bind();

    topDownCamera.computeProjectionMatrix();
    const QMatrix4x4& projMtx = topDownCamera.getProjectionMatrix();
    
    s_pShader->setUniformValue("ProjectionMatrix", projMtx);
    
    topDownCamera.computeViewMatrix();
    const QMatrix4x4& modelViewMtx = topDownCamera.getViewMatrix();
    
    s_pShader->setUniformValue("ModelViewMatrix", modelViewMtx);
    
    //setup color lookup table
    glBindTexture(GL_TEXTURE_1D, m_colorTextureID);

    //draw the three lists
    //use the paged node vao for outlines
    //use the ebo for the inside of the paged nodes
    glBindVertexArray(m_pagedNodeVAO);

    s_pShader->setUniformValue("ColorIndex",
                               static_cast<int>(PAGED_NODE_NUM_STATES));//black outline
    
    std::list<PagedOctTreeNode*> activeList;
    std::list<PagedOctTreeNode*> inactiveList;
    std::list<PagedOctTreeNode*> loadingList;
    BuildDrawListsAndDrawOutlines(m_pagedNodeDrawables, activeList, inactiveList, loadingList);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_pagedNodeEBO);

    //inside of paged nodes are transparent
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    //this sets the color
    s_pShader->setUniformValue("ColorIndex",
                               static_cast<int>(PAGED_NODE_ACTIVE));

    DrawPagedNodes(activeList);
    
    s_pShader->setUniformValue("ColorIndex",
                               static_cast<int>(PAGED_NODE_LOADING));
    DrawPagedNodes(loadingList);

    s_pShader->setUniformValue("ColorIndex",
                               static_cast<int>(PAGED_NODE_INACTIVE));
    DrawPagedNodes(inactiveList);
    //draw frustum
	glDisable(GL_DEPTH_TEST);

    const QMatrix4x4& mainCamViewMtx = camera.getViewMatrix();
    
    QMatrix4x4 invMainCamViewMtx = mainCamViewMtx.inverted();

    s_pShader->setUniformValue("WorldTransform", invMainCamViewMtx);

    glBindVertexArray(m_frustumVAO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_frustumEBO);

    s_pShader->setUniformValue("ColorIndex",
                               static_cast<int>(PAGED_NODE_NUM_STATES)+1);
    
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

    s_pShader->release();

    glBindVertexArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_1D, 0);

    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, camera.getViewportWidth(), camera.getViewportHeight());
}