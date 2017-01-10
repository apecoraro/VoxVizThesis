#ifndef VOXVIZOPENGL_OPENGL_WINDOW_H
#define VOXVIZOPENGL_OPENGL_WINDOW_H

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLWidget.h"
#include "VoxVizCore/Camera.h"

#include <QtGui/qmainwindow.h>
#include <QtGui/qlabel.h>
#include <QtGui/qscrollarea.h>
#include <QtGui/qgridlayout.h>

namespace voxOpenGL
{
    class GLWindow : public QMainWindow
    {
    public:
        GLWindow(vox::Camera& camera,
                 QGLContext* pContext);

        GLWidget& getGLWidget() { return *m_pGLWidget; }
    private:
        QSize getSize();

        QWidget *m_pCentralWidget;
        QScrollArea *m_pGLWidgetArea;
        GLWidget *m_pGLWidget;
    };
}
#endif
