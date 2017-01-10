#include "Ray.h"

//#include "Sphere.h"
//#include "Box.h"
//#include "Triangle.h"
//#include "Polygon.h"
//#include "Ellipsoid.h"
//#include "Matrix.h"

#include "VoxVizCore/Plane.h"

//#include <float.h>

using namespace vox;

Ray::Ray(const QVector3D& start, const QVector3D& dir) :
    _start(start),
    _dir(dir)
{
}

QVector3D Ray::getPoint(double tval) const
{
    return _start + (_dir * tval);
}

//float IntersectSphereWithRay(const QVector3D& sphereCenter,
//                             float sphereRadiusSq,
//                             const QVector3D& rayOrigin,
//                             const QVector3D& rayDir)
//{
//    //use the quadractic equation to solve for time to intersect
//    //at2 + bt + c = 0 (solve for t)
//    float a = rayDir.lengthSq();
//    QVector3D vecCenterToRayStart(rayOrigin - sphereCenter);
//    float b = 2 * QVector3D::dot(rayDir, vecCenterToRayStart);
//    float c = vecCenterToRayStart.lengthSq() - sphereRadiusSq;
//
//    float b24ac = (b*b) - (4*a*c);
//    if(b24ac < 0.0f)
//        return -1.0f;//no intersection
//    else
//    {
//        float a2 = (2*a);
//        float sqrt_b24ac = sqrtf(b24ac);
//
//        float tvalPlus = (-b + sqrt_b24ac)/a2;
//        if(tvalPlus < 0.0f)
//            return -1.0f;//no intersection (sphere is behind ray)
//        else
//        {
//            float tvalMinus = (-b - sqrt_b24ac)/a2;
//            float isectTVal;
//            if(tvalMinus < 0.0f)
//                isectTVal = tvalPlus;//start point is inside sphere
//            else
//                isectTVal = tvalMinus;
//
//            return isectTVal;
//        }
//    }
//}
//
//bool Ray::intersect(const BoundingSphere& sphere, float& t) const
//{
//    t = IntersectSphereWithRay(sphere.center(), sphere.radiusSq(), _start, _dir);
//    return t >= 0.0f;
//}
//
//float Ray::intersect(const Sphere& sphere, QVector3D& isectPt, QVector3D& isectNorm) const
//{
//    float t = IntersectSphereWithRay(sphere.center(), sphere.radiusSq(), 
//                                  _start, _dir);
//    if(t >= 0.0f)
//    {
//        isectPt = _start + (_dir * t);
//        isectNorm = isectPt - sphere.center();
//    }
//
//    return t;
//}


//static void ComputeRayPlaneIntersectionInterval(const Ray& ray, const Plane& plane,  ISectData& isectInterval)
//{
//    float dirDotN = QVector3D::dot(ray.direction(), plane.normal());
//    QVector3D vecPlaneToRayStart(ray.start() - plane.pointOnPlane());
//    float vecPlaneToRSDotN = QVector3D::dot(vecPlaneToRayStart, plane.normal());
//    if(dirDotN > 0)
//    {
//        float tIsect = - (vecPlaneToRSDotN) / dirDotN;
//        if(tIsect < isectInterval.tMax)
//        {
//            isectInterval.tMax = tIsect;
//            isectInterval.normalMax = plane.normal();
//        }
//    }
//    else if(dirDotN < 0)
//    {
//        float tIsect = - (vecPlaneToRSDotN) / dirDotN;
//        if(tIsect > isectInterval.tMin)
//        {
//            isectInterval.tMin = tIsect;
//            isectInterval.normalMin = plane.normal();
//        }
//    }
//    else if(vecPlaneToRSDotN > 0)
//    {
//        isectInterval.tMin = FLT_MAX;
//        isectInterval.tMax = -FLT_MAX;
//    }
//}

//float Ray::intersect(const Box& box, QVector3D& isectPt, QVector3D& isectNorm) const
//{
//    ISectData isectInterval;
//    for(size_t i = 0; i < 6 && 
//        isectInterval.tMin <= isectInterval.tMax; ++i)
//    {
//        const Plane& plane = box.getPlane(i);
//        ComputeRayPlaneIntersectionInterval(*this, plane, isectInterval);        
//    }
//
//    if(isectInterval.tMin > isectInterval.tMax)
//        return -1.0f;//no intersection
//    
//    float tIsect = isectInterval.tMin;
//    isectNorm = isectInterval.normalMin;
//
//    if(isectInterval.tMin == 0.0f)
//    {
//        tIsect = isectInterval.tMax;
//        isectNorm = isectInterval.normalMax;
//    }
//
//    isectPt = getPoint(tIsect);
//    return tIsect;
//}

//static float DistRayPlane(const QVector3D& rayOrigin, const QVector3D& rayDirection, 
//                              const QVector3D& planeNormal, float planeD)
//{
//    float cosAlpha = QVector3D::dot(planeNormal, rayDirection);
//
//    // no intersection
//    if (fabs(cosAlpha) < 0.001f) 
//        return -1.0f;//ray is parallel to plane
//
//    return (-(planeD + QVector3D::dot(planeNormal, rayOrigin)) / cosAlpha);
//}
//
//static bool IntersectTriangleWithRay(const Triangle& triangle,
//                                     const QVector3D& rayOrigin,
//                                     const QVector3D& rayDirection,
//                                     QVector3D& intersection,
//                                     float& dist, 
//                                     float& u,
//                                     float& v)
//{
//    const QVector3D& p1 = triangle.points(0);
//    const QVector3D& p2 = triangle.points(1);
//    const QVector3D& p3 = triangle.points(2);
//
//    //compute the plane that the triangle sits in
//    Plane triPlane(p1, triangle.normal());
//    const QVector3D& normal = triPlane.normal();
//    //calculate the plane equation for this triangle to see if the ray intersects
//    float planeD = triPlane.planeD();
//
//    dist = DistRayPlane(rayOrigin, rayDirection, normal, planeD);
//    if(dist >= 0.0f)//if positive then the plane is in front of the plane and not parallel
//    {
//        intersection.x() = rayOrigin.x() + (rayDirection.x() * dist);
//        intersection.y() = rayOrigin.y() + (rayDirection.y() * dist);
//        intersection.z() = rayOrigin.z() + (rayDirection.z() * dist);
//
//        //check to see if the intersection point is contained by  the poly
//        return triangle.contains(intersection, u, v);
//    }
//
//    return false;
//}

//float Ray::intersect(const Polygon& polygon, 
//                     QVector3D& isectPt, 
//                     QVector3D& isectNorm, 
//                     bool& isCurveShaded, 
//                     QVector3D& shadingNorm,
//                     bool& hasTexCoords,
//                     Vec2& texCoords) const
//{
//    for(size_t triIndex = 0; triIndex < polygon.getNumTriangles(); ++triIndex)
//    {
//        const Triangle& triangle = polygon.getTriangle(triIndex);
//        
//        float dist;
//        float u,v;
//        if(IntersectTriangleWithRay(triangle, _start, _dir, isectPt, dist, u, v))
//        {
//            isectNorm = triangle.normal();
//            if(!triangle.isFlatShaded())
//            {
//                isCurveShaded = true;
//                shadingNorm = triangle.normal(u, v);
//            }
//            else
//                isCurveShaded = false;
//
//            if(triangle.hasTexCoords())
//            {
//                hasTexCoords = true;
//                texCoords = triangle.texCoord(u, v);
//            }
//            else
//                hasTexCoords = false;
//
//            return dist;
//        }
//    }
//
//    return -1.0f;
//}
//
//float Ray::intersect(const Ellipsoid& ellipsoid, QVector3D& isectPt, QVector3D& isectNorm) const
//{
//    const Matrix& unitSphereToEllipsoidMtx = ellipsoid.unitSphereToEllipsoidMatrix();
//    const Matrix& unitSphereNormMtx = ellipsoid.unitSphereNormToEllipsoidNormMatrix();
//    const Matrix& ellipsoidToUnitSphereMtx = ellipsoid.ellipsoidToUnitSphereMatrix();
//
//    QVector3D sphereRayOrigin = ellipsoidToUnitSphereMtx.transformPoint(_start);
//    QVector3D sphereRayDir = ellipsoidToUnitSphereMtx.transformVector(_dir);
//
//    QVector3D sphereCenter(0.0f, 0.0f, 0.0f);
//    float tval = IntersectSphereWithRay(sphereCenter, 1.0f, sphereRayOrigin, sphereRayDir);
//
//    if(tval >= 0.0f)
//    {
//        isectPt = sphereRayOrigin + (sphereRayDir * tval);
//        isectNorm = isectPt - sphereCenter;
//    
//        isectPt = unitSphereToEllipsoidMtx.transformPoint(isectPt);
//        isectNorm = unitSphereNormMtx.transformVector(isectNorm);
//    }
//    
//    return tval;
//}
//
//bool Ray::intersect(const PlaneList& planes, ISectData& isectInterval) const
//{
//    for(size_t i = 0; i < planes.size() && 
//        isectInterval.tMin <= isectInterval.tMax; ++i)
//    {
//        const Plane& plane = planes.at(i);
//        ComputeRayPlaneIntersectionInterval(*this, plane, isectInterval);        
//    }
//
//    return (isectInterval.tMin > isectInterval.tMax);
//}

bool Ray::intersect(const Plane& plane, double& t) const
{
    double dirDotN = QVector3D::dotProduct(_dir, plane.normal());
    QVector3D vecPlaneToRayStart(_start - plane.pointOnPlane());
    double vecPlaneToRSDotN = QVector3D::dotProduct(vecPlaneToRayStart, plane.normal());
    
    if(dirDotN == 0.0f && vecPlaneToRSDotN > 0)
    {
        return false;//direction and normal are 90 degree (so ray is paralell) and ray start is not on the plane
    }
    
    t = - (vecPlaneToRSDotN) / dirDotN;
        
    return (t > 0.0f);
}
