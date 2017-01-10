#include "VoxVizCore/Camera.h"
#include "VoxVizOpenGL/GLUtils.h"

#include <math.h>

// define the standard trig values
#ifdef PI
#undef PI
#undef PI_2
#undef PI_4
#endif
const double PI   = 3.14159265358979323846;
const double PI_2 = 1.57079632679489661923;
const double PI_4 = 0.78539816339744830962;
const double DEG2RAD = (PI/180.0);

vox::Camera::Camera() : 
    m_up(0.0f,1.0f,0.0f),
    m_look(0.0f,0.0f,1.0f),
    m_lookAt(0, 0, 0),
    m_left(1.0f,0.0f,0.0f),
    m_worldUp(0.0f, 0.0f, 1.0f),
    m_yaw(0),
    m_pitch(0),
    m_roll(0),
    m_moveAmount(0.1f),
    m_rotAmount(0.01f),
    m_viewportX(0),
    m_viewportY(0),
    m_viewportWidth(800),
    m_viewportHeight(600),
    //m_viewportWidth(820),
    //m_viewportWidth(448+20),
    //m_viewportHeight(639),
    //m_viewportHeight(448+39),
    m_fieldOfView(45.0f),
    m_aspectRatio(static_cast<float>(m_viewportWidth) / 
                    static_cast<float>(m_viewportHeight)),
    m_nearPlaneDist(1.0f),
    m_farPlaneDist(10000.0f),
    m_frameCount(0)
{
}

static void ComputeFrustum(double *matrix,
                           double left,
                           double right,
                           double bottom,
                           double top,
                           double znear,
                           double zfar)
{
    double temp, temp2, temp3, temp4;
    temp = 2.0 * znear;
    temp2 = right - left;
    temp3 = top - bottom;
    temp4 = zfar - znear;
    matrix[0] = temp / temp2;
    matrix[1] = 0.0;
    matrix[2] = 0.0;
    matrix[3] = 0.0;
    matrix[4] = 0.0;
    matrix[5] = temp / temp3;
    matrix[6] = 0.0;
    matrix[7] = 0.0;
    matrix[8] = (right + left) / temp2;
    matrix[9] = (top + bottom) / temp3;
    matrix[10] = (-zfar - znear) / temp4;
    matrix[11] = -1.0;
    matrix[12] = 0.0;
    matrix[13] = 0.0;
    matrix[14] = (-temp * zfar) / temp4;
    matrix[15] = 0.0;
}

static void ComputePerspective(double *matrix, 
                               double fovyInDegrees,
                               double aspectRatio,
                               double znear,
                               double zfar)
{
    double ymax, xmax;
    //double temp, temp2, temp3, temp4;
    ymax = znear * tanf(fovyInDegrees * PI / 360.0);
    //ymin = -ymax;
    //xmin = -ymax * aspectRatio;
    xmax = ymax * aspectRatio;
    ComputeFrustum(matrix, -xmax, xmax, -ymax, ymax, znear, zfar);
}

void vox::Camera::computeProjectionMatrix()
{
    ComputePerspective(m_projMatrix.data(),
                       m_fieldOfView,
                       m_aspectRatio,
                       m_nearPlaneDist,
                       m_farPlaneDist);
}

void vox::Camera::setOpenGLProjectionMatrix()
{
    glMatrixMode(GL_PROJECTION);

    glLoadMatrixd(m_projMatrix.data());

}

void vox::Camera::setOpenGLViewMatrix()
{
    glMatrixMode(GL_MODELVIEW);

    glLoadMatrixd(m_viewMatrix.data());
}

void vox::Camera::setYPR(float yaw,float pitch, float roll)
{
    m_yaw = yaw;
    m_pitch = pitch;
    m_roll = roll;
}

void vox::Camera::getNearPlaneWidthHeight(float& viewPlaneWidth, float& viewPlaneHeight) const
{
    viewPlaneWidth = m_nearPlaneDist * tan(m_fieldOfView / 2.0f) * 2.0f;
    viewPlaneHeight = viewPlaneWidth * m_aspectRatio;
}

float vox::Camera::getPixelSize() const
{
    //float viewPlaneWidth;
    //float viewPlaneHeight;
    //getNearPlaneWidthHeight(viewPlaneWidth, viewPlaneHeight);

    //float horizPixelSize = viewPlaneWidth / m_viewportWidth;
    //float vertPixelSize = viewPlaneHeight / m_viewportHeight;

    //return std::max(horizPixelSize, vertPixelSize);
    float size = 0.0f;
    
    //{
    //    float fieldOfViewX = m_fieldOfView * DEG2RAD;
    //    float fieldOfViewY = (static_cast<float>(m_viewportHeight) / static_cast<float>(m_viewportWidth)) * fieldOfViewX;
    //    int u = 0;
    //    float x = (((2.0f*u)-m_viewportWidth)/m_viewportWidth) * tan(fieldOfViewX);
    //    int v = 0;
    //    float y = (((2.0f*v)-m_viewportHeight)/m_viewportHeight) * tan(fieldOfViewY);
    //    QVector3D nearPlaneCenter = m_position + (m_look * m_nearPlaneDist);
    //    QVector3D viewPlaneLL = nearPlaneCenter + (m_left * x) + (m_up * y);
    //    QVector3D camToVPLL(viewPlaneLL - m_position);
    //    camToVPLL.normalize();
    //    u = 1;
    //    x = (((2.0f*u)-m_viewportWidth)/m_viewportWidth) * tan(fieldOfViewX);
    //    v = 1;
    //    y = (((2.0f*v)-m_viewportHeight)/m_viewportHeight) * tan(fieldOfViewY);
    //    QVector3D viewPlaneUR = nearPlaneCenter + (m_left * x) + (m_up * y);;
    //    QVector3D camToVPUR(viewPlaneUR - m_position);
    //    camToVPUR.normalize();
    //    size = 1.0f - QVector3D::dotProduct(camToVPLL, camToVPUR);
    //}
    {
        GLint viewport[4] = 
        { 
            m_viewportX, 
            m_viewportY,
            m_viewportWidth,
            m_viewportHeight
        };

        double worldX, worldY, worldZ;
        double winX = static_cast<double>(m_viewportWidth) * 0.5;
        double winY = static_cast<double>(m_viewportHeight) * 0.5;
        voxOpenGL::GLUtils::ComputeWorldXYZ(winX, 
                                            winY,
                                            0.0,
                                            m_viewMatrix.data(),
                                            m_projMatrix.data(),
                                            viewport, 
                                            worldX, worldY, worldZ);
        QVector3D viewPlaneLL(worldX, worldY, worldZ);
        QVector3D nearPlaneCenter = m_position + (m_look * m_nearPlaneDist);
        QVector3D camToVPLL(viewPlaneLL - m_position);
        camToVPLL.normalize();
        winX += 1;
        winY += 1;
        voxOpenGL::GLUtils::ComputeWorldXYZ(winX, 
                                            winY,
                                            0.0,
                                            m_viewMatrix.data(),
                                            m_projMatrix.data(),
                                            viewport,
                                            worldX, worldY, worldZ);
        QVector3D viewPlaneUR(worldX, worldY, worldZ);
        QVector3D camToVPUR(viewPlaneUR - m_position);
        camToVPUR.normalize();
        size = 1.0f - QVector3D::dotProduct(camToVPLL, camToVPUR);
    }
    //{
    //float halfPixelWidth = ((1.0f/(float)m_viewportWidth)/44.0f);

    //float halfPixelHeight = ((1.0f/(float)m_viewportHeight)/44.0f);
    //QVector3D position = m_position / 44.0f;
    //float nearPlaneDist = m_nearPlaneDist / 44.0f;
    //QVector3D nearPlaneCenter = m_position + (m_look * nearPlaneDist);
    //QVector3D viewPlaneLL = nearPlaneCenter - (m_up * halfPixelHeight) + (m_left * halfPixelWidth);
    //QVector3D camToVPLL(viewPlaneLL - position);
    //camToVPLL.normalize();

    //QVector3D viewPlaneUR = nearPlaneCenter + (m_up * halfPixelHeight) - (m_left * halfPixelWidth);
    //QVector3D camToVPUR(viewPlaneUR - position);
    //camToVPUR.normalize();

    //size = 1.0f - QVector3D::dotProduct(camToVPLL, camToVPUR);
    //}

    //{
    //float halfPixelWidth = (1.0f/(float)m_viewportWidth);

    //float halfPixelHeight = (1.0f/(float)m_viewportHeight);

    //QVector3D nearPlaneCenter = m_position + (m_look * m_nearPlaneDist);
    //QVector3D viewPlaneLL = nearPlaneCenter - (m_up * halfPixelHeight) + (m_left * halfPixelWidth);
    //QVector3D camToVPLL(viewPlaneLL - m_position);
    //camToVPLL.normalize();

    //QVector3D viewPlaneUR = nearPlaneCenter + (m_up * halfPixelHeight) - (m_left * halfPixelWidth);
    //QVector3D camToVPUR(viewPlaneUR - m_position);
    //camToVPUR.normalize();

    //size = 1.0f - QVector3D::dotProduct(camToVPLL, camToVPUR);
    //return size;
    //}
    return size;
}

void vox::Camera::moveForward(float scalar)
{
    QVector3D moveVec = (m_look * (m_moveAmount * scalar));
    m_position += moveVec;
    m_lookAt += moveVec;
}

void vox::Camera::moveUp(float scalar)
{
    QVector3D moveVec = (m_up * (m_moveAmount * scalar));
    m_position += moveVec;
    m_lookAt += moveVec;
}

void vox::Camera::moveRight(float scalar)
{
    QVector3D moveVec = (m_left * (m_moveAmount * -scalar));
    m_position += moveVec;
    m_lookAt += moveVec;
}

static float ClampTo360(float angle)
{
    while(angle>2*PI)
        angle-=2*PI;

    while(angle<0)
        angle+=2*PI;

    return angle;
}

void vox::Camera::yaw(float scalar)
{
    m_yaw += (m_rotAmount * scalar);
    m_yaw = ClampTo360(m_yaw);
}   

void vox::Camera::pitch(float scalar)
{
    m_pitch += (m_rotAmount * scalar);
    m_pitch = ClampTo360(m_pitch);
}

void vox::Camera::roll(float scalar) 
{
    m_roll += (m_rotAmount * scalar);
    m_roll = ClampTo360(m_roll);
}

vox::ModelCamera::ModelCamera() : m_offset(5.0f)
{
}

static void AnglesToAxes(const QVector3D& angles, QVector3D& left, QVector3D& up, QVector3D& forward)
{
    float sx, sy, sz, cx, cy, cz, theta;

    // rotation angle about X-axis (pitch)
    theta = angles.x();
    sx = sinf(theta);
    cx = cosf(theta);

    // rotation angle about Y-axis (yaw)
    theta = angles.y();
    sy = sinf(theta);
    cy = cosf(theta);

    // rotation angle about Z-axis (roll)
    theta = angles.z();
    sz = sinf(theta);
    cz = cosf(theta);

    // determine left axis
    left.setX(cy*cz);
    left.setY(sx*sy*cz + cx*sz);
    left.setZ(-cx*sy*cz + sx*sz);

    // determine up axis
    up.setX(-cy*sz);
    up.setY(-sx*sy*sz + cx*cz);
    up.setZ(cx*sy*sz + sx*cz);

    // determine forward axis
    forward.setX(sy);
    forward.setY(-sx*cy);
    forward.setZ(cx*cy);
}

void vox::ModelCamera::computeViewMatrix()
{
    AnglesToAxes(QVector3D(m_pitch, m_yaw, m_roll),
                 m_left, m_up, m_look);
                
    double *m = m_viewMatrix.data();
    //const QVector3D& eVec = m_position;
    const QVector3D& u = -m_left;
    const QVector3D& v = m_up;
    const QVector3D& n = -m_look;
        
    QVector3D eVec = m_lookAt + (n * m_offset);

    m[0] = u.x(); m[4] = u.y(); m[8] = u.z(); m[12] = -QVector3D::dotProduct(eVec, u);
    m[1] = v.x(); m[5] = v.y(); m[9] = v.z(); m[13] = -QVector3D::dotProduct(eVec, v);
    m[2] = n.x(); m[6] = n.y(); m[10] = n.z(); m[14] = -QVector3D::dotProduct(eVec, n);
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}

vox::FlyCamera::FlyCamera() : m_flyRoll(0.0f)
{
    m_look = QVector3D(0.0, 1.0, 0.0);
    m_left = QVector3D(-1.0, 0.0, 0.0);
    m_up = QVector3D(0.0, 0.0, 1.0);
}

void vox::FlyCamera::yaw(float scalar)
{
    setLookAt(m_lookAt + (m_left * -scalar));
}   

void vox::FlyCamera::pitch(float scalar)
{
    setLookAt(m_lookAt + (m_up * scalar));
}

void vox::FlyCamera::roll(float scalar) 
{
    m_flyRoll = scalar * 0.01f;
}

void vox::FlyCamera::computeViewMatrix()
{
    m_look = m_lookAt - m_position;
    m_look.normalize();
    
    m_left = QVector3D::crossProduct(m_worldUp, m_look);
    m_left.normalize();
    if(m_flyRoll != 0.0f)
    {
        m_worldUp += (m_left * m_flyRoll);
        m_worldUp.normalize();
        m_left = QVector3D::crossProduct(m_worldUp, m_look);
        m_left.normalize();

        m_flyRoll = 0.0f;
    }

    m_up = QVector3D::crossProduct(m_look, m_left);
    m_up.normalize();
    //Rotate viewdir around the up vector:
	double *m = m_viewMatrix.data();
    //const QVector3D& eVec = m_position;
    const QVector3D& u = -m_left;
    const QVector3D& v = m_up;
    const QVector3D& n = -m_look;
        
    QVector3D eVec = m_position;

    m[0] = u.x(); m[4] = u.y(); m[8] = u.z(); m[12] = -QVector3D::dotProduct(eVec, u);
    m[1] = v.x(); m[5] = v.y(); m[9] = v.z(); m[13] = -QVector3D::dotProduct(eVec, v);
    m[2] = n.x(); m[6] = n.y(); m[10] = n.z(); m[14] = -QVector3D::dotProduct(eVec, n);
    m[3] = 0.0f; m[7] = 0.0f; m[11] = 0.0f; m[15] = 1.0f;
}