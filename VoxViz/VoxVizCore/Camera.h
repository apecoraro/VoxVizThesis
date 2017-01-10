#ifndef VOX_CAMERA_H
#define VOX_CAMERA_H

#include <QtGui/qvector3d.h>
#include <QtGui/qmatrix4x4.h>

namespace vox
{
    class Camera
    {
    protected:
        QMatrix4x4 m_projMatrix;
        QMatrix4x4 m_viewMatrix;
        QVector3D m_position; // camera position
        QVector3D m_up;
        QVector3D m_look;
        QVector3D m_lookAt;
        QVector3D m_left;
        QVector3D m_worldUp;
        float m_yaw;    // rot y axis
        float m_pitch;  // rot x axis
        float m_roll;   // rot z axis
        float m_moveAmount;
        float m_rotAmount;

        int m_viewportX;
        int m_viewportY;
        int m_viewportWidth;
        int m_viewportHeight;

        float m_fieldOfView;
        float m_aspectRatio;
        float m_nearPlaneDist;
        float m_farPlaneDist;

        size_t m_frameCount;

    public:
        Camera();
        ~Camera(void){};

        void computeProjectionMatrix();
        const double* getProjectionMatrixPtr() const { return m_projMatrix.data(); }
        const QMatrix4x4& getProjectionMatrix() const { return m_projMatrix; }
        void setOpenGLProjectionMatrix();

        virtual void computeViewMatrix() = 0;
        const double* getViewMatrixPtr() const { return m_viewMatrix.data(); }
        const QMatrix4x4& getViewMatrix() const { return m_viewMatrix; }
        void setOpenGLViewMatrix();

        // Sets
        void setPosition(const QVector3D& pos) { m_position = pos; }
        void setLookAt(const QVector3D& lookAt) { m_lookAt = lookAt; }
        void setWorldUp(const QVector3D& worldUp) { m_worldUp = worldUp; }
        void setYPR(float yaw,float pitch, float roll); 
        void setMoveAmount(float amt) { m_moveAmount = amt; }
        void setRotationAmount(float amt) { m_rotAmount = amt; }

        void setFieldOfView(float fov) { m_fieldOfView = fov; }
        void setAspectRatio(float aspRat) { m_aspectRatio = aspRat; }
        void setNearPlaneDist(float npd) { m_nearPlaneDist = npd; }
        void setFarPlaneDist(float fpd) { m_farPlaneDist = fpd; }

        void setViewport(int x, int y, int w, int h)
        {
            m_viewportX = x;
            m_viewportY = y;
            m_viewportWidth = w;
            m_viewportHeight = h;

            m_aspectRatio = static_cast<float>(m_viewportWidth) / 
                             static_cast<float>(m_viewportHeight);
        }

        void setFrameCount(size_t fc) { m_frameCount = fc; }

        // Gets
        float getYaw() const {return m_yaw;}
        float getPitch() const {return m_pitch;}
        float getRoll() const {return m_roll;}
        virtual QVector3D getPosition() const
        {
            return m_position;
        }

        const QVector3D& getLook() const { return m_look; }
        const QVector3D& getUp() const { return m_up; }
        const QVector3D& getLeft() const { return m_left; }

        float getFieldOfView() const { return m_fieldOfView; }
        float getAspectRatio() const { return m_aspectRatio; }
        float getNearPlaneDist() const { return m_nearPlaneDist; }
        float getFarPlaneDist() const { return m_farPlaneDist; }

        void getViewportWidthHeight(int& width, int& height) const
        {
            width = m_viewportWidth;
            height = m_viewportHeight;
        }

        int getViewportWidth() const { return m_viewportWidth; }
        int getViewportHeight() const { return m_viewportHeight; }

        void getNearPlaneWidthHeight(float& width, float& height) const;

        float getPixelSize() const;

        size_t getFrameCount() const { return m_frameCount; }

        // Move operations
        virtual void moveForward(float scalar);
        virtual void moveUp(float scalar);
        virtual void moveRight(float scalar);

        // Rotations
        virtual void yaw(float scalar); // rotate around y axis
        virtual void pitch(float scalar); // rotate around x axis
        virtual void roll(float scalar); // rotate around z axis    
    };

    class ModelCamera : public Camera
    {
    private:
        float m_offset; // camera position
        
    public:
        ModelCamera();

        virtual void computeViewMatrix();

        void setOffset(float offset) { m_offset = offset; }

        virtual QVector3D getPosition() const
        {
            const_cast<QVector3D&>(m_position) = m_lookAt - (m_look * m_offset);
            return m_position;
        }
        
        virtual void moveForward(float scalar) { m_offset += (m_moveAmount * -scalar); }
    };

    class FlyCamera : public Camera
    {
    private:
        float m_flyRoll;
    public:
        FlyCamera();

        virtual void computeViewMatrix();

        virtual void yaw(float scalar); // rotate around y axis
        virtual void pitch(float scalar); // rotate around x axis
        virtual void roll(float scalar); // rotate around z axis  
    };
};

#endif
