#pragma once

#include <QtGui/qvector3d.h>
//#include "Vec2.h"
//#include "BoundingVolumes.h"

//#include <float.h>
//#include <vector>

namespace vox
{
    //class Sphere;
    //class Box;
    class Plane;
    //class Polygon;
    //class Ellipsoid;

    //struct ISectData
    //{
    //    float tMin;
    //    float tMax;
    //    QVector3D normalMin;
    //    QVector3D normalMax;
    //    ISectData() : tMin(0.0f), tMax(FLT_MAX) {}
    //};

    class Ray
    {
    public:
        Ray(const QVector3D& start, const QVector3D& dir);

        QVector3D getPoint(double tval) const;
        //float intersect(const Sphere& sphere, QVector3D& isectPt, QVector3D& isectNorm) const;
        //float intersect(const Box& box, QVector3D& isectPt, QVector3D& isectNorm) const;
        //float intersect(const Polygon& polygon, 
        //                QVector3D& isectPt, 
        //                QVector3D& isectNorm, 
        //                bool& isCurveShaded, QVector3D& shadingNorm,
        //                bool& hasTexCoords, Vec2& texCoords) const;
        //float intersect(const Ellipsoid& ellipsoid, QVector3D& isectPt, QVector3D& isectNorm) const;

        //typedef std::vector<Plane> PlaneList;

        //bool intersect(const PlaneList& planeList, ISectData& isectData) const;
        bool intersect(const Plane& plane, double& t) const;
        //bool intersect(const BoundingSphere& sphere, float& t) const;

        const QVector3D& start() const { return _start; }
        QVector3D& start() { return _start; }
        const QVector3D& direction() const { return _dir; }
        QVector3D& direction() { return _dir; }

        void setStart(const QVector3D& start) { _start = start; }
        void setDirection(const QVector3D& dir) { _dir = dir; }
    private:
        QVector3D _start;
        QVector3D _dir;
    };
};
