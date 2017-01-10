#include "VoxVizOpenGL/GLWindow.h"

using namespace voxOpenGL;

GLWindow::GLWindow(vox::Camera& camera,
                   QGLContext* pContext)
{
    m_pCentralWidget = new QWidget;
    setCentralWidget(m_pCentralWidget);

    m_pGLWidgetArea = new QScrollArea;
    
    m_pGLWidget = new GLWidget(camera, 
                               pContext, 
                               m_pGLWidgetArea->viewport());

    m_pGLWidgetArea->setWidget(m_pGLWidget);
    m_pGLWidgetArea->setWidgetResizable(true);
    
    QGridLayout *pCentralLayout = new QGridLayout;
    pCentralLayout->addWidget(m_pGLWidgetArea, 0, 0);

    m_pCentralWidget->setLayout(pCentralLayout);

    setWindowTitle(tr("VoxVizViewer"));
    int winWidth, winHeight;
    camera.getViewportWidthHeight(winWidth, winHeight);
    //get desired viewport width & height
    //add extra pixels for border of window
    winWidth += 20;//these amounts determined by trial and error
    winHeight += 39;
    setGeometry(20, 20, winWidth, winHeight);
    //this will trigger a resize event, which should end up setting the
    //viewport to the desired values
    //setGeometry(20, 20, 820, 639);
    //setGeometry(20, 20, 120, 139);
}

QSize GLWindow::getSize()
{
    return m_pGLWidget->size();
}
