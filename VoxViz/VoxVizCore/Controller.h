#ifndef VOX_CONTROLLER_H
#define VOX_CONTROLLER_H

#include <QtCore/qobject.h>

#include "VoxVizCore/SceneObject.h"

namespace vox
{
    class Controller : public QObject, public Referenced
    {
    private:
        SceneObject* pSceneObject;
    };
}
#endif
