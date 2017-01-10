#include "BoundingVolumes.h"

using namespace vox;

BoundingSphere::BoundingSphere() :
    m_center(0.0f, 0.0f, 0.0f),
    m_radius(-1.0f),
    m_radiusSq(-1.0f)
{
}
        
BoundingSphere::BoundingSphere(const QVector3D& center,
                               double radius) :
    m_center(center),
    m_radius(radius),
    m_radiusSq(radius * radius)
{
}

BoundingSphere::~BoundingSphere()
{
}

const QVector3D& BoundingSphere::getCenter() const
{
    return m_center;
}

double BoundingSphere::getRadius() const
{
    return m_radius;
}
       
bool BoundingSphere::contains(const QVector3D& pos) const
{
    QVector3D diff = pos - m_center;

    double lenSq = diff.lengthSquared();

    return lenSq < m_radiusSq;
}

static bool Valid(const BoundingSphere& bsphere)
{
    return bsphere.radius() > 0.0;
}

void BoundingSphere::expandBy(const QVector3D& center, float radius)
{
    BoundingSphere sh(center, radius);
    // ignore operation if incomming BoundingSphere is invalid.
    if(!Valid(sh)) 
        return;

    // This sphere is not set so use the inbound sphere
    if(!Valid(*this))
    {
        m_center = sh.m_center;
        m_radius = sh.m_radius;
        m_radiusSq = sh.m_radiusSq;

        return;
    }
    
    // Calculate d == The distance between the sphere centers   
    double d = ( m_center - sh.center() ).length();

    // New sphere is already inside this one
    if(d + sh.radius() <= m_radius)  
    {
        return;
    }

    //  New sphere completely contains this one 
    if(d + m_radius <= sh.radius())  
    {
        m_center = sh.m_center;
        m_radius = sh.m_radius;
        m_radiusSq = sh.m_radiusSq;
        return;
    }

    // Build a new sphere that completely contains the other two:
    //
    // The center point lies halfway along the line between the furthest
    // points on the edges of the two spheres.
    //
    // Computing those two points is ugly - so we'll use similar triangles
    double new_radius = (m_radius + d + sh.radius() ) * 0.5;
    double ratio = ( new_radius - m_radius ) / d ;

    double x = m_center.x();
    double y = m_center.y();
    double z = m_center.z();

    x += (sh.center().x() - x) * ratio;
    y += (sh.center().y() - y) * ratio;
    z += (sh.center().z() - z) * ratio;

    m_center.setX(x);
    m_center.setY(y);
    m_center.setZ(z);

    m_radius = new_radius;
    m_radiusSq = m_radius * m_radius;
}