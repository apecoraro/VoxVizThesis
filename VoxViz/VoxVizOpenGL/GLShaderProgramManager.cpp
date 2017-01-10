#include "VoxVizOpenGL/GLShaderProgramManager.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"

#include "VoxVizCore/SmartPtr.h"
#include "VoxVizCore/Referenced.h"

#include <iostream>

using namespace voxOpenGL;

class RefShader : public vox::Referenced
{
private:
    QGLShader* m_pShader;
public:
    RefShader(QGLShader* pShader) : m_pShader(pShader)
    {
        //nothing
    }
    QGLShader* getShader() { return m_pShader; }
protected:
    virtual ~RefShader() 
    {
        if(m_pShader)
            delete m_pShader;
    }
};

typedef std::map<std::string, vox::SmartPtr<RefShader> > Shaders;

Shaders s_loadedShaders;

static void AddShader(ShaderProgram* pProg,
                      QGLShader::ShaderType type,
                      const std::string& shaderFile)
{
    Shaders::iterator findIt = s_loadedShaders.find(shaderFile);
    if(findIt != s_loadedShaders.end())
    {
        pProg->addShader(findIt->second->getShader());
    }
    else
    {
        QGLShader* pShader = new QGLShader(type);
        pShader->compileSourceFile(QString(shaderFile.c_str()));
        pProg->addShader(pShader);

        s_loadedShaders[shaderFile] = new RefShader(pShader);
    }
}

static bool RecompileShader(const std::string& shaderFile)
{
    return s_loadedShaders[shaderFile]->getShader()->compileSourceFile(QString(shaderFile.c_str()));
}

ShaderProgram::ShaderProgram(const std::string& vertShaderFile,
                             const std::string& fragShaderFile,
                             QObject * parent) :
    QGLShaderProgram(parent),
    m_vertShaderFile(vertShaderFile),
    m_fragShaderFile(fragShaderFile),
    m_onReloadFuncPtr(nullptr)
{
    AddShader(this, QGLShader::Vertex, m_vertShaderFile);
    AddShader(this, QGLShader::Fragment, m_fragShaderFile);
}

ShaderProgram::ShaderProgram(const std::string& vertShaderFile,
                             const std::string& fragShaderFile,
                             const QGLContext * context, QObject * parent) :
    QGLShaderProgram(context, parent),
    m_vertShaderFile(vertShaderFile),
    m_fragShaderFile(fragShaderFile)
{
    AddShader(this, QGLShader::Vertex, m_vertShaderFile);
    AddShader(this, QGLShader::Fragment, m_fragShaderFile);
}

bool ShaderProgram::getUniformValue(const char* pName, double* pDblBuf, size_t bufSize) const
{
    float* pFloatBuf = new float[bufSize];
    bool retValue = false;
    if(getUniformValue(pName, pFloatBuf))
    {
        for(size_t i = 0; i < bufSize; ++i)
        {
            pDblBuf[i] = pFloatBuf[i];
        }

        retValue = true;
    }

    delete [] pFloatBuf;

    return retValue;
}

bool ShaderProgram::getUniformValue(const char* pName, float* pFloatBuf) const
{
    GLint location = uniformLocation(pName);
    if(location == -1)
        return false;

    glGetUniformfv(programId(), location, pFloatBuf);

    return true;
}

bool ShaderProgram::getUniformValue(const char* pName, int* pIntBuf) const
{
    GLint location = uniformLocation(pName);
    if(location == -1)
        return false;

    glGetUniformiv(programId(), location, pIntBuf);

    return true;
}

bool ShaderProgram::getUniformValue(const char* pName, unsigned int* pUIntBuf) const
{
    GLint location = uniformLocation(pName);
    if(location == -1)
        return false;

    glGetUniformuiv(programId(), location, pUIntBuf);
    
    return true;
}

bool ShaderProgram::getUniformValue(const char* pName, bool& boolVal) const
{
    GLint location = uniformLocation(pName);
    if(location == -1)
        return false;

    int boolInt;
    glGetUniformiv(programId(), location, &boolInt);

    boolVal = boolInt != 0;

    return true;
}

bool ShaderProgram::setUniformUIntValue(const std::string& name,
                                    unsigned int value)
{
    GLint location = uniformLocation(name.c_str());
    if(location == -1)
        return false;

    glUniform1ui(location, value);

    return true;
}

void ShaderProgram::bindFragDataLocation(const char* pName, int index)
{
    GLUtils::CheckOpenGLError();

    glBindFragDataLocation(programId(), index, pName);

    GLUtils::CheckOpenGLError();
}

bool ShaderProgram::reloadShaderFiles()
{
    bool vert = RecompileShader(m_vertShaderFile.c_str());
    bool frag = RecompileShader(m_fragShaderFile.c_str());
    if(vert && frag)
    {
        link();
        
        if(m_onReloadFuncPtr != nullptr)
            m_onReloadFuncPtr(this);
        return true;
    }

    return false;
}


ShaderProgramManager& ShaderProgramManager::instance()
{
    static ShaderProgramManager s_instance;

    return s_instance;
}

ShaderProgram* ShaderProgramManager::createShaderProgram(const std::string& vertShaderFile,
                                                         const std::string& fragShaderFile)
{
    m_pGLContext->makeCurrent();

    std::string vsf = m_shaderDirectory + "/" + vertShaderFile;
    std::string fsf = m_shaderDirectory + "/" + fragShaderFile;

    ShaderProgram* pProgram = new ShaderProgram(vsf, fsf);

    pProgram->link();

    m_pGLContext->doneCurrent();

    m_shaderPrograms.push_back(pProgram);

    return pProgram;
}

void ShaderProgramManager::setShaderDirectory(const std::string& directory)
{
    m_shaderDirectory = directory;
}
 
ShaderProgramManager::ShaderProgramManager() : m_shaderDirectory(".") {}

ShaderProgramManager::~ShaderProgramManager()
{
    for(size_t i = 0; i < m_shaderPrograms.size(); ++i)
        delete m_shaderPrograms.at(i);
}
