#ifndef VoxOpenGL_GLShaderManager_H
#define VoxOpenGL_GLShaderManager_H

#include "VoxVizOpenGL/GLExtensions.h"

#include <QtOpenGL/qglshaderprogram.h>
#include <QtCore/qobject.h>

#include <vector>

namespace voxOpenGL
{
    class ShaderProgram : public QGLShaderProgram
    {
    private:
        std::string m_vertShaderFile;
        std::string m_fragShaderFile;
        void (*m_onReloadFuncPtr)(ShaderProgram*);
    public:
         ShaderProgram(const std::string& vertShaderFile,
                       const std::string& fragShaderFile,
                       QObject * parent = 0);

         ShaderProgram(const std::string& vertShaderFile,
                       const std::string& fragShaderFile,
                       const QGLContext * context, QObject * parent = 0);

         bool getUniformValue(const char* pName, double* pDblBuf, size_t bufSize=1) const;
         bool getUniformValue(const char* pName, float* pFloatBuf) const;
         bool getUniformValue(const char* pName, int* pIntBuf) const;
         bool getUniformValue(const char* pName, unsigned int* pUIntBuf) const;
         bool getUniformValue(const char* pName, bool& boolVal) const;

         bool setUniformUIntValue(const std::string& name,
                                  unsigned int value);

         void bindFragDataLocation(const char* pName, int index);

         bool reloadShaderFiles();
         void setOnReloadFunc(void (*onReloadFuncPtr)(ShaderProgram*))
         {
             m_onReloadFuncPtr = onReloadFuncPtr;
         }
    };

    class ShaderProgramManager  : public QObject
    {
    public:
        static ShaderProgramManager& instance();

        ShaderProgram* createShaderProgram(const std::string& vertShaderFile,
                                           const std::string& fragShaderFile);

        void setShaderDirectory(const std::string& directory);
        void setContext(QGLContext* pContext) { m_pGLContext = pContext; }
        QGLContext* getContext() { return m_pGLContext; }
    protected:
        ShaderProgramManager();
        ~ShaderProgramManager();
    private:
        QGLContext* m_pGLContext;
        std::string m_shaderDirectory;
        typedef std::vector<ShaderProgram*> ShaderPrograms;
        ShaderPrograms m_shaderPrograms;
    };
}

#endif
