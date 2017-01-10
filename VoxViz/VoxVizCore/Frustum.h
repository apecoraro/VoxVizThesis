#ifndef _FRUSTUM_H_
#define _FRUSTUM_H_

#include <QtGui/QVector3D>

#include "VoxVizCore/Plane.h"
#include "VoxVizCore/Camera.h"

namespace vox
{
    class Frustum
    {
    public:

	    enum RESULT
        {
            OUTSIDE, 
            INTERSECT, 
            INSIDE
        };

	    qreal frustum[6][4];

	    Frustum();
	    ~Frustum();

        void setFromCamera(const vox::Camera& camera);
	    RESULT sphereInFrustum(const QVector3D &p, float radius, float& distance);
    };
}


#endif