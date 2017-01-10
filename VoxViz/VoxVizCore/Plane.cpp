#include "Plane.h"

using namespace vox;

//calculates the D in the Ax + By + Cz + D = 0 plane equation
static double CalcPlaneD(const QVector3D& pointOnPlane, const QVector3D& planeNormal)
{
    double Ax = (planeNormal.x() * pointOnPlane.x());
    double By = (planeNormal.y() * pointOnPlane.y());
    double Cz = (planeNormal.z() * pointOnPlane.z());
    return (-Ax - By - Cz);
}

Plane::Plane(const QVector3D& pointOnPlane, const QVector3D& normal) :
    _pointOnPlane(pointOnPlane),
    _normal(normal),
    _planeD(CalcPlaneD(_pointOnPlane, _normal))
{
}

void Plane::set(const QVector3D& pointOnPlane, const QVector3D& normal)
{
    _pointOnPlane = pointOnPlane;
    _normal = normal;
    _planeD = CalcPlaneD(_pointOnPlane, _normal);
}

void Plane::setNormalAndPoint(const QVector3D& normal, const QVector3D& pointOnPlane)
{
    set(pointOnPlane, normal);
}

void Plane::set3Points(QVector3D &v1,  QVector3D &v2,  QVector3D &v3) 
{
	QVector3D aux1, aux2;

	aux1 = v1 - v2;
	aux2 = v3 - v2;

	QVector3D normal = aux2 * aux1;

	normal.normalize();
    set(v2, normal);
}