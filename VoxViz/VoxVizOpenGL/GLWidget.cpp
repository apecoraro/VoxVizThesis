#include "VoxVizOpenGL/GLWidget.h"
#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"

#include "VoxVizCore/Camera.h"

#include <QtOpenGL/QGLContext>

#include <GL/gl.h>
#include <GL/glu.h>

#include "NvUI/NvUI.h"

#include <iostream>

using namespace voxOpenGL;

static bool s_glewInitialized = false;

GLWidget::GLWidget(vox::Camera& camera,
                  QGLContext* pContext,
                  QWidget *parent/*=NULL*/)
    : QGLWidget(pContext, parent), 
      m_xRot(0), 
      m_yRot(0), 
      m_zRot(0),
      m_camera(camera),
      m_frameCount(0),
      m_pBckgrdGlCtx(nullptr),
      m_pGUIWindow(nullptr),
      m_pFramesPerSecondUI(nullptr)
{
    //QGLFormat glFmt = pContext->format();
    //glFmt.setSwapInterval(-1);
    //m_pBckgrdGlCtx = new QGLPixelBuffer(800, 600, glFmt, this);
    //m_pBckgrdGlCtx->create(pContext);

    makeCurrent();

    if(!s_glewInitialized)
    {
        GLenum err = glewInit();
        if(GLEW_OK == err)
            s_glewInitialized = true;
        else
        {
            std::cerr << "Error: glewInit() failed - " << glewGetErrorString(err) << std::endl;
        }
    }

    doneCurrent();

    setFocusPolicy(Qt::ClickFocus);
    setFocus();
    setUpdatesEnabled(false);
}

GLWidget::~GLWidget()
{
    makeCurrent();
    if(m_pGUIWindow != nullptr)
        delete m_pGUIWindow;
}

void GLWidget::addSceneObject(vox::SceneObject* pObj)
{
    m_sceneObjects.push_back(pObj);
}

void GLWidget::removeSceneObject(vox::SceneObject* pObj)
{
    for(SceneObjects::iterator itr = m_sceneObjects.begin();
        itr != m_sceneObjects.end();
        ++itr)
    {
        if(pObj == itr->get())
        {
            m_sceneObjects.erase(itr);
            break;
        }
    }
}

vox::SceneObject* GLWidget::getSceneObject(size_t index)
{
    return m_sceneObjects.at(index).get();
}

const vox::SceneObject* GLWidget::getSceneObject(size_t index) const
{
    return m_sceneObjects.at(index).get();
}

size_t GLWidget::getNumSceneObjects() const
{
    return m_sceneObjects.size();
}

void GLWidget::initializeGL()
{
    glEnable(GL_DEPTH_TEST);

    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    
    QSize widgetSize = size();

    m_camera.setViewport(0, 0, widgetSize.width(), widgetSize.height());

    //voxOpenGL::GLUtils::InitScreenAlignedQuad();
    //voxOpenGL::GLUtils::InitNearPlaneQuad(m_camera);
    //voxOpenGL::GLUtils::SetClearColorAndDepth(0.5f, 0.5f, 0.5f, 1.0f,
    //                                          1.0f);
    int w, h;
    m_camera.getViewportWidthHeight(w, h);
    m_pGUIWindow = new NvUIWindow((float)w, (float)h);

    for(size_t i = 0; i < m_sceneObjects.size(); ++i)
    {
        vox::SceneObject* pObj = m_sceneObjects.at(i).get();
        
        vox::Renderer* pRenderer = pObj->getRenderer();
        if(pRenderer)
        {
            pRenderer->init(m_camera, *pObj);
            pRenderer->initGUI(*m_pGUIWindow);
        }
    }

    m_pFramesPerSecondUI = vox::Renderer::CreateGUIValueText("FPS: ", 20.0f, 0.0f, 0);
    m_pGUIWindow->Add(m_pFramesPerSecondUI, 10, 10);
    m_frameTimer.start();

    m_timerID = startTimer(0);
}

void GLWidget::glDraw()
{
    makeCurrent();
    paintGL();
    swapBuffers();
    postDraw();
}

void GLWidget::postDraw()
{
    for(size_t i = 0; i < m_sceneObjects.size(); ++i)
    {
        vox::SceneObject* pObj = m_sceneObjects.at(i).get();
        vox::Renderer* pRenderer = pObj->getRenderer();
        if(pRenderer)
        {
            pRenderer->postDraw(m_camera, *pObj);
        }
    }
}

#include <fstream>

static void DumpCameraParamsToFile(const vox::Camera& camera)
{
	static std::ofstream cameraParamsFile("camera_params.txt");
	if(cameraParamsFile.is_open() == false)
		return;

	cameraParamsFile.seekp(0);
	cameraParamsFile << "POSITION "
					 << camera.getPosition().x()
					 << " "
					 << camera.getPosition().y()
					 << " "
					 << camera.getPosition().z()
					 << std::endl;

	QVector3D lookAt = camera.getPosition() + (camera.getLook() * 1000.0f);
	cameraParamsFile << "LOOK_AT "
					 << lookAt.x()
					 << " "
					 << lookAt.y()
					 << " "
					 << lookAt.z()
					 << std::endl;

	cameraParamsFile << "NEAR " << camera.getNearPlaneDist() << std::endl;

	cameraParamsFile << "FAR " << camera.getFarPlaneDist() << std::endl;
}

void GLWidget::paintGL()
{
	GLUtils::CheckOpenGLError();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	
    m_camera.setFrameCount(m_frameCount);

    ++m_frameCount;

    m_camera.computeViewMatrix();

	DumpCameraParamsToFile(m_camera);

    for(size_t i = 0; i < m_sceneObjects.size(); ++i)
    {
        vox::SceneObject* pObj = m_sceneObjects.at(i).get();
        vox::Renderer* pRenderer = pObj->getRenderer();
        if(pRenderer)
        {
            pRenderer->draw(m_camera, *pObj);
            if(pRenderer->getDrawGUI())
            {
                NvUST time = 0;
                int w, h;
                m_camera.getViewportWidthHeight(w, h);
                NvUIDrawState ds(time, w, h);
                m_pGUIWindow->Draw(ds);
            }
        }
    }

    for(size_t i = 0; i < m_sceneObjects.size(); ++i)
    {
        vox::SceneObject* pObj = m_sceneObjects.at(i).get();
        vox::Renderer* pRenderer = pObj->getRenderer();
        if(pRenderer)
        {
            pRenderer->update(m_camera, *pObj);
            if(pRenderer->getDrawGUI())
                pRenderer->updateGUI(*m_pGUIWindow);
        }
    }
	
    GLUtils::CheckOpenGLError();

    updateFPS();
}

void GLWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);

    m_camera.setViewport(0, 0, width, height);

    m_camera.computeProjectionMatrix();
}

void GLWidget::mousePressEvent(QMouseEvent *evt)
{
    m_lastPos = evt->pos();
}

void GLWidget::mouseMoveEvent(QMouseEvent *evt)
{
    int dx = evt->x() - m_lastPos.x();
    int dy = evt->y() - m_lastPos.y();

    if(evt->buttons() & Qt::LeftButton) 
    {
        m_camera.yaw(dx);
        m_camera.pitch(-dy);
    }
    else if(evt->buttons() & Qt::RightButton)
    {
        m_camera.roll(dx);
    }
    m_lastPos = evt->pos();
}

void GLWidget::keyPressEvent(QKeyEvent *evt)
{
    switch(evt->key())
    {
    case Qt::Key_W:
        m_camera.moveForward(5.0f);
        break;
    case Qt::Key_S:
        m_camera.moveForward(-5.0f);
        break;
    case Qt::Key_D:
        m_camera.moveRight(5.0f);
        break;
    case Qt::Key_A:
        m_camera.moveRight(-5.0f);
        break;
    case Qt::Key_E:
        m_camera.moveUp(5.0f);
        break;
    case Qt::Key_C:
        m_camera.moveUp(-5.0f);
        break;
    case Qt::Key_R:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            std::cout << "Reload shaders." << std::endl;
            pRenderer->setReloadShaders(true);
        }
        break;
    case Qt::Key_0:
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            int newDebugMode = evt->key() - Qt::Key_0;
            int curDebugMode = pRenderer->getDrawDebug();
            if(curDebugMode == newDebugMode)
                newDebugMode = 0;
            pRenderer->setDrawDebug(newDebugMode);
            std::cout << "Draw Debug: " << newDebugMode << std::endl;
        }
        break;
    case Qt::Key_G:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            pRenderer->setDrawGUI(!pRenderer->getDrawGUI());
            std::cout << "Draw GUI: " << (pRenderer->getDrawGUI() ? "yes" : "no") << std::endl;
        }
        break;
	case Qt::Key_L:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            pRenderer->setComputeLighting(!pRenderer->getComputeLighting());
            std::cout << "Compute Lighting: " << (pRenderer->getComputeLighting() ? "yes" : "no") << std::endl;
        }
        break;
	case Qt::Key_Up:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            pRenderer->setLodScalar(pRenderer->getLodScalar() + 0.5f);
            std::cout << "LOD: " << pRenderer->getLodScalar() << std::endl;
        }
        break;
	case Qt::Key_Down:
        for(size_t i = 0; i < m_sceneObjects.size(); ++i)
        {
            vox::Renderer* pRenderer = m_sceneObjects.at(i)->getRenderer();
            pRenderer->setLodScalar(pRenderer->getLodScalar() - 0.5f);
            std::cout << "LOD: " << pRenderer->getLodScalar() << std::endl;
        }
        break;
   }
}

void GLWidget::timerEvent(QTimerEvent*)
{
	glDraw();
}

void GLWidget::updateFPS()
{
    qint64 elapsed = m_frameTimer.restart();

    if(elapsed > 0)
    {
        float fps = 1000.0f / static_cast<float>(elapsed);

        m_pFramesPerSecondUI->SetValue(fps);
    }
}
