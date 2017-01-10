#ifndef VOXVIZOPENGL_GLWIDGET_H
#define VOXVIZOPENGL_GLWIDGET_H

#include "VoxVizOpenGL/GLExtensions.h"

#include "VoxVizCore/SceneObject.h"
#include "VoxVizCore/Camera.h"

#include <QtGui/qevent.h>
#include <QtOpenGL/qgl.h>
#include <QtGui/QLabel>
#include <QtCore/qelapsedtimer.h>
#include <QtOpenGL/QGLPixelBuffer>

#include "NvUI/NvUI.h"

#include <vector>

namespace voxOpenGL
{
    class GLWidget : public QGLWidget
    {
    private:
        int m_xRot;
        int m_yRot;
        int m_zRot;
        QPoint m_lastPos;
        vox::Camera& m_camera;
        size_t m_frameCount;

        typedef std::vector< vox::SmartPtr<vox::SceneObject> > SceneObjects;
        SceneObjects m_sceneObjects;
        QGLPixelBuffer* m_pBckgrdGlCtx;

        NvUIWindow *m_pGUIWindow;
        NvUIValueText* m_pFramesPerSecondUI;
        QElapsedTimer m_frameTimer;

		int m_timerID;
    public:
        GLWidget(vox::Camera& camera,
                 QGLContext* pContext,
                 QWidget *parent=NULL);
        ~GLWidget();

        QGLPixelBuffer* getBackgroundContext() { return m_pBckgrdGlCtx; }

        vox::Camera& getCamera() { return m_camera; }
        const vox::Camera& getCamera() const { return m_camera; }

        void addSceneObject(vox::SceneObject* pObj);
        void removeSceneObject(vox::SceneObject* pObj);
        vox::SceneObject* getSceneObject(size_t index);
        const vox::SceneObject* getSceneObject(size_t index) const;
        size_t getNumSceneObjects() const;
 
    protected:
        void initializeGL();
        virtual void glDraw() override;
        void postDraw();
        virtual void paintGL() override;
        virtual void resizeGL(int width, int height) override;
        void setXRotation(int angle);
        void setYRotation(int angle);
        void setZRotation(int angle);
        virtual void mousePressEvent(QMouseEvent *evt) override;
        virtual void mouseMoveEvent(QMouseEvent *evt) override;
        virtual void keyPressEvent(QKeyEvent *evt) override;
        virtual void timerEvent(QTimerEvent *evt) override;
        void updateFPS();
    };
}
#endif
