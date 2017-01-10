#pragma once

#include <QtGui/qvector3d.h>

namespace vox
{
    class Plane
    {
    public:
        Plane() {}
        Plane(const QVector3D& pointOnPlane, const QVector3D& normal);

        void set(const QVector3D& pointOnPlane, const QVector3D& normal);
        void setNormalAndPoint(const QVector3D& normal, const QVector3D& pointOnPlane);
        void set3Points(QVector3D &v1,  QVector3D &v2,  QVector3D &v3);

        const QVector3D& pointOnPlane() const { return _pointOnPlane; }
        const QVector3D& normal() const { return _normal; }
        double planeD() const { return _planeD; }
        double eval(const QVector3D& p) const { return _normal.x()*p.x() + _normal.y()*p.y() + _normal.z()*p.z() + _planeD; }
    private:
        QVector3D _pointOnPlane;
        QVector3D _normal;
        //The D from the plane equation Ax + By + Cz + D = 0
        double _planeD;
    };
};
