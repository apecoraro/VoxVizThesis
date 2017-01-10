#ifndef VOX_BOUNDING_VOLUMES_H
#define VOX_BOUNDING_VOLUMES_H

#include <QtGui/qvector3d.h>

#include <float.h>
#include <math.h>

namespace vox
{
    class BoundingSphere
    {
    public:
        BoundingSphere();
        BoundingSphere(const QVector3D& center,
                       double radius);
        ~BoundingSphere();

        const QVector3D& getCenter() const;
        const QVector3D& center() const { return getCenter(); }
        double getRadius() const;
        double radius() const { return getRadius(); }

        void setRadius(double r) { m_radius = r; m_radiusSq = r * r; }
        void setCenter(const QVector3D& c) { m_center = c; }

        bool valid() const { return m_radius > 0.0f; }
        bool contains(const QVector3D& pos) const;

        void expandBy(const QVector3D& center, float radius);
    private:

        QVector3D m_center;
        double m_radius;
        double m_radiusSq;
    };

    
    class BoundingBox
    {
    private:
        QVector3D m_min;
        QVector3D m_max;

    public:
        inline BoundingBox() :
            m_min(FLT_MAX,
                  FLT_MAX,
                  FLT_MAX),
            m_max(-FLT_MAX,
                  -FLT_MAX,
                  -FLT_MAX)
        {}
    
        inline BoundingBox(double xmin, double ymin, double zmin,
                            double xmax, double ymax, double zmax) :
                            m_min(xmin,ymin,zmin),
                            m_max(xmax,ymax,zmax) {}

        inline BoundingBox(const QVector3D& min,const QVector3D& max) : 
                    m_min(min),
                    m_max(max) {}

        inline void init()
        {
            m_min.setX(FLT_MAX);
            m_min.setY(FLT_MAX);
            m_min.setZ(FLT_MAX);
            m_max.setX(-FLT_MAX);
            m_max.setY(-FLT_MAX);
            m_max.setZ(-FLT_MAX);
        }
        
        inline bool valid() const
        {
            return m_max.x()>=m_min.x() &&  m_max.y()>=m_min.y() &&  m_max.z()>=m_min.z();
        }

        inline void set (double xmin, double ymin, double zmin,
                            double xmax, double ymax, double zmax)
        {
            m_min = QVector3D(xmin,ymin,zmin);
            m_max = QVector3D(xmax,ymax,zmax);
        }

        inline void set(const QVector3D& min,const QVector3D& max)
        {
            m_min = min;
            m_max = max;
        }


        inline double xMin() const { return m_min.x(); }
 
        inline double yMin() const { return m_min.y(); }
 
        inline double zMin() const { return m_min.z(); }

        inline double xMax() const { return m_max.x(); }
 
        inline double yMax() const { return m_max.y(); }
 
        inline double zMax() const { return m_max.z(); }

        inline QVector3D center() const
        {
            return (m_min+m_max)*0.5;
        }

        inline const QVector3D& minimum() const
        {
            return m_min;
        }

        inline const QVector3D& maximum() const
        {
            return m_max;
        }

        inline double radius() const
        {
            return sqrt(radius2());
        }

        inline double radius2() const
        {
            return 0.25*((m_max-m_min).lengthSquared());
        }

        inline const QVector3D corner(unsigned int pos) const
        {
            return QVector3D(pos&1?m_max.x():m_min.x(),pos&2?m_max.y():m_min.y(),pos&4?m_max.z():m_min.z());
        }

        inline void expandBy(const QVector3D& v)
        {
            if(v.x()<m_min.x()) m_min.setX(v.x());
            if(v.x()>m_max.x()) m_max.setX(v.x());

            if(v.y()<m_min.y()) m_min.setY(v.y());
            if(v.y()>m_max.y()) m_max.setY(v.y());

            if(v.z()<m_min.z()) m_min.setZ(v.z());
            if(v.z()>m_max.z()) m_max.setZ(v.z());
        }

        /** Expands the bounding box to include the given coordinate.
            * If the box is uninitialized, set its min and max extents to
            * Vec3(x,y,z). */
        inline void expandBy(double x,double y,double z)
        {
            if(x<m_min.x()) m_min.setX(x);
            if(x>m_max.x()) m_max.setX(x);

            if(y<m_min.y()) m_min.setY(y);
            if(y>m_max.y()) m_max.setY(y);

            if(z<m_min.z()) m_min.setZ(z);
            if(z>m_max.z()) m_max.setZ(z);
        }

        /** Expands this bounding box to include the given bounding box.
            * If this box is uninitialized, set it equal to bb. */
        void expandBy(const BoundingBox& bb)
        {
            if (!bb.valid()) return;

            if(bb.m_min.x()<m_min.x()) m_min.setX(bb.m_min.x());
            if(bb.m_max.x()>m_max.x()) m_max.setX(bb.m_max.x());

            if(bb.m_min.y()<m_min.y()) m_min.setY(bb.m_min.y());
            if(bb.m_max.y()>m_max.y()) m_max.setY(bb.m_max.y());

            if(bb.m_min.z()<m_min.z()) m_min.setZ(bb.m_min.z());
            if(bb.m_max.z()>m_max.z()) m_max.setZ(bb.m_max.z());
        }

        /** Expands this bounding box to include the given sphere.
            * If this box is uninitialized, set it to include sh. */
        void expandBy(const BoundingSphere& sh)
        {
            if (!sh.valid()) return;

            if(sh.center().x()-sh.radius()<m_min.x()) m_min.setX(sh.center().x()-sh.radius());
            if(sh.center().x()+sh.radius()>m_max.x()) m_max.setX(sh.center().x()+sh.radius());

            if(sh.center().y()-sh.radius()<m_min.y()) m_min.setY(sh.center().y()-sh.radius());
            if(sh.center().y()+sh.radius()>m_max.y()) m_max.setY(sh.center().y()+sh.radius());

            if(sh.center().z()-sh.radius()<m_min.z()) m_min.setZ(sh.center().z()-sh.radius());
            if(sh.center().z()+sh.radius()>m_max.z()) m_max.setZ(sh.center().z()+sh.radius());
        }

        /** Returns true if this bounding box contains the specified coordinate. */
        inline bool contains(const QVector3D& v) const
        {
            return valid() && 
                    (v.x()>=m_min.x() && v.x()<=m_max.x()) &&
                    (v.y()>=m_min.y() && v.y()<=m_max.y()) &&
                    (v.z()>=m_min.z() && v.z()<=m_max.z());
        }
    };

};

#endif
