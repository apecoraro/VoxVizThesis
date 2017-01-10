#include "VoxVizCore/SmartPtr.h"
#include "VoxVizCore/DataSetReader.h"
#include "VoxVizCore/DataSetWriter.h"
#include "VoxVizCore/VolumeDataSet.h"
#include "VoxVizCore/Renderer.h"

#include "MarchingCubes/MarchingCubesRenderer.h"
#include "VolumeSlicer3D/VolumeSlicer3DRenderer.h"
#include "RayCaster/RayCastRenderer.h"
#include "GigaVoxels/GigaVoxelsRenderer.h"

#include "VoxVizOpenGL/GLWindow.h"
#include "VoxVizOpenGL/GLShaderProgramManager.h"
#include "VoxVizOpenGL/GLUtils.h"

#include <QtGui/qapplication.h>
#include <QtCore/qlist.h>
#include <QtCore/qstring.h>
#include <QtCore/qstringlist.h>

#include <NvAssetLoader/NvAssetLoader.h>

#include <iostream>
#include <sstream>
#include <fstream>

static void PrintUsage()
{
    std::cerr << "VoxViz "
                 "--input <name of input file> "
                 "--shaders <path to shader directory> "
                 "[--algorithm < mc | rc (default) | vs | gv > ] "
                 "[--samples < number of samples; defaults to max volume dimension > ] "
				 "[--camera-params start-x start-y start-z look-x look-y look-z] "
				 "[--camera-scalars move-amt rot-amt] " 
              << std::endl;
}

static double ParseFloat(const std::string& param)
{
    double value;
    std::stringstream floatStr;
    floatStr << param;
    floatStr >> value;

    return value;
}

static void ParseFloat(const std::string& param,
                       float& value)
{
    std::stringstream floatStr;
    floatStr << param;
    floatStr >> value;
}

bool ParseCameraParamsFile(const char* pCameraParamsFile,
						   QVector3D& cameraPos,
						   QVector3D& cameraLookAt,
						   float& cameraNear,
					       float& cameraFar)
{
    std::ifstream cameraParamsFile(pCameraParamsFile);
    if(cameraParamsFile.is_open() == false)
        return false;

    while(cameraParamsFile.eof() == false)
    {
        std::string token;
        cameraParamsFile >> token;
        if(token == "POSITION")
        {
            std::string param;
            cameraParamsFile >> param;
            cameraPos.setX(ParseFloat(param));
            
            cameraParamsFile >> param;
            cameraPos.setY(ParseFloat(param));

            cameraParamsFile >> param;
            cameraPos.setZ(ParseFloat(param));
        }
        else if(token == "LOOK_AT")
        {
            std::string param;
            cameraParamsFile >> param;
            cameraLookAt.setX(ParseFloat(param));
            
            cameraParamsFile >> param;
            cameraLookAt.setY(ParseFloat(param));

            cameraParamsFile >> param;
            cameraLookAt.setZ(ParseFloat(param));
        }
        else if(token == "NEAR")
        {
            std::string param;
            cameraParamsFile >> param;
            ParseFloat(param, cameraNear);
        }
        else if(token == "FAR")
        {
            std::string param;
            cameraParamsFile >> param;
            ParseFloat(param, cameraFar);
        }
    }

    return true;
}

bool ParseCmdLineArgs(const QCoreApplication& app,
                      std::string& inputFile,
                      std::string& algorithm,
                      std::string& shaderDir,
                      std::string& cameraType,
                      size_t& numSamples,
                      bool& cliCamera,
                      QVector3D& cameraPos,
                      QVector3D& cameraLookAt,
					  float& cameraNear,
					  float& cameraFar,
                      float& cameraMoveAmt,
                      float& cameraRotAmt,
                      bool& noLighting,
                      std::stringstream& errorMessage)
{
    const QStringList& args = app.arguments();
    for(int i = 1; i < args.size(); ++i)
    {
        const QString& arg = args[i];
        if(arg == "--input")
        {
            inputFile = args[i+1].toAscii().data();
            ++i;
        }
        else if(arg == "--algorithm")
        {
            algorithm = args[i+1].toAscii().data();
            ++i;
        }
        else if(arg == "--shaders")
        {
            shaderDir = args[i+1].toAscii().data();
            ++i;
        }
        else if(arg == "--camera")
        {
            cameraType = args[i+1].toAscii().data();
            ++i;
        }
        else if(arg == "--samples")
        {
            numSamples = args[i+1].toLong();
            ++i;
        }
		else if(arg == "--camera-params-file")
		{
			if(!ParseCameraParamsFile(args[++i].toAscii().data(),
								     cameraPos, cameraLookAt,
									 cameraNear,
									 cameraFar))
				return false;

			cliCamera = true;
		}
        else if(arg == "--camera-params")
        {
            cameraPos.setX(args[++i].toDouble());
            cameraPos.setY(args[++i].toDouble());
            cameraPos.setZ(args[++i].toDouble());

            cameraLookAt.setX(args[++i].toDouble());
            cameraLookAt.setY(args[++i].toDouble());
            cameraLookAt.setZ(args[++i].toDouble());

            cliCamera = true;
        }
        else if(arg == "--camera-scalars")
        {
            cameraMoveAmt = args[++i].toDouble();
            cameraRotAmt = args[++i].toDouble();
        }
		else if(arg == "--camera-near")
		{
			cameraNear = args[++i].toDouble();
		}
		else if(arg == "--camera-far")
		{
			cameraFar = args[++i].toDouble();
		}
        else if(arg == "--no-lighting")
        {
            noLighting = true;
        }
    }

    return inputFile.size() > 0 
           && algorithm.size() > 0;
}

static void Cleanup()
{
    //std::cout << "HERE" << std::endl;
}

void NVWindowsLog(const char* fmt, ...)
{
    const int length = 1024;
    char buffer[length];
    va_list ap;
  
    va_start(ap, fmt); 
    vsnprintf_s(buffer, length-1, fmt, ap);
    std::cout << buffer << std::endl;
    va_end(ap);
}

void checkGLError(const char* file, int32_t line)
{
    GLint error = glGetError();
    if (error)
    {
        const char* errorString = 0;
        switch(error)
        {
            case GL_INVALID_ENUM: errorString="GL_INVALID_ENUM"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: errorString="GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            case GL_INVALID_VALUE: errorString="GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errorString="GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: errorString="GL_OUT_OF_MEMORY"; break;
            default: errorString = "unknown error"; break;
        }
        printf("GL error: %s, line %d: %s\n", file, line, errorString);
        error = 0; // nice place to hang a breakpoint in compiler... :)
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    NvAssetLoaderInit(NULL);

    qAddPostRoutine(Cleanup);

    std::string inputFile;
    std::string algorithm = "rc";
    std::string shaderDirectory;
    std::string cameraType = "fly";
    size_t numSamples = 0;
    bool cliCamera = false;
    QVector3D cameraPos(36.0, 30.0, -280.0);
    QVector3D cameraLookAt(-300.0, -254.0, -363.0);
    float cameraMoveAmt = -1.0f;
    float cameraRotAmt = -1.0f;
	float cameraNear = 1.0f;
	float cameraFar = 10000.0f;
    bool noLighting = false;
    std::stringstream errorMessage;

    if(!ParseCmdLineArgs(app,
                     inputFile,
                     algorithm,
                     shaderDirectory,
                     cameraType,
                     numSamples,
                     cliCamera,
                     cameraPos,
                     cameraLookAt,
					 cameraNear,
					 cameraFar,
                     cameraMoveAmt,
                     cameraRotAmt,
                     noLighting,
                     errorMessage))
    {
        std::cerr << errorMessage.str() << std::endl;
        PrintUsage();
        std::cout << "Hit Enter to exit..." << std::endl;
        char key;
        std::cin.get(key);
        std::cin.clear();
        return 1;
    }

    std::cout << "Arguments:"
              << std::endl
              << "Camera Position: "
              << cameraPos.x() << ", " << cameraPos.y() << ", " << cameraPos.z()
              << std::endl
              << "Camera LookAt: "
              << cameraLookAt.x() << ", " << cameraLookAt.y() << ", " << cameraLookAt.z()
              << std::endl;

    //read in a volume dataset
    vox::DataSetReader reader;

    vox::SmartPtr<vox::VolumeDataSet> spDataSet = reader.readVolumeDataFile(inputFile);

    if(!spDataSet.valid())
    {
        std::cerr << "Error: unable to read input file: " << inputFile << std::endl;
        std::cout << "Hit Enter to exit..." << std::endl;
        char key;
        std::cin.get(key);
        std::cin.clear();
        return 1;
    }

    if(numSamples > 0)
        spDataSet->setNumSamples(numSamples);
	 
    const vox::BoundingBox& bbox = spDataSet->getBoundingBox();

    vox::Camera* pCamera;
    vox::ModelCamera modelCamera;
    vox::FlyCamera flyCamera;
    if(cameraType == "model")
    {
        pCamera = &modelCamera;

        if(spDataSet->getData() != nullptr)
        {
            modelCamera.setOffset(bbox.radius()*2.5f);
        }
        else
        {
            //random default values
            modelCamera.setOffset(250.0f);
        }
    }
    else
        pCamera = &flyCamera;

    if(pCamera == &flyCamera)
    {
        pCamera->setPosition(cameraPos);
        pCamera->setLookAt(cameraLookAt);
    }
    else if(bbox.valid())
    {
        pCamera->setPosition(bbox.center() 
                             + (pCamera->getLook() * bbox.radius() * -2.5f));
        pCamera->setLookAt(bbox.center());
    }
    else
    {
        //random default values
        //pCamera->setYPR(0.81000000, 4.7223892, 3.1415925);
        //pCamera->setPosition(QVector3D(-1.6, -1.4, 80.0));
        pCamera->setPosition(QVector3D(0, -10, 100.0));
        pCamera->setLookAt(QVector3D(0.0, 0.0, 80.0));
    }

	pCamera->setNearPlaneDist(cameraNear);
	pCamera->setFarPlaneDist(cameraFar);

    if(cameraMoveAmt > 0.0f)
        pCamera->setMoveAmount(cameraMoveAmt);
    if(cameraRotAmt > 0.0f)
        pCamera->setRotationAmount(cameraRotAmt);
    
    QGLFormat glFmt(QGL::DoubleBuffer
                    | QGL::DepthBuffer
                    | QGL::Rgba
                    | QGL::AlphaChannel
                    | QGL::StencilBuffer
                    | QGL::DirectRendering
                    | QGL::SampleBuffers);

    glFmt.setSamples(4);
    glFmt.setSwapInterval(-1);

//    glFmt.setVersion(4, 2);

    QGLContext* pGlCtx = new QGLContext(glFmt);
    voxOpenGL::GLWindow glWindow(*pCamera, pGlCtx);

    //if(!pGlCtx->isSharing())
    //{
    //    std::cerr << "Unable to create shared context." << std::endl;
    //    return 1;
    //}

    voxOpenGL::ShaderProgramManager::instance().setShaderDirectory(shaderDirectory);
    voxOpenGL::ShaderProgramManager::instance().setContext(pGlCtx);
    voxOpenGL::GLUtils::Initialize();
	gv::GigaVoxelsRenderer::RegisterRenderer();
	rc::RayCastRenderer::RegisterRenderer();
	vs3D::VolumeSlicer3DRenderer::RegisterRenderer();
	mc::MarchingCubesRenderer::RegisterRenderer();

    //feed it into a volume renderer
    vox::SmartPtr<vox::Renderer> spRenderer = vox::Renderer::CreateRenderer(algorithm);
    if(spRenderer.get() == NULL)
    {
        std::cerr << "Error: unknown rendering algorithm " << algorithm << std::endl;
        std::cout << "Hit Enter to exit..." << std::endl;
        char key;
        std::cin.get(key);
        std::cin.clear();
        return 1;
    }

    if(spRenderer->acceptsFileExtension(vox::DataSetReader::GetFileExtension(inputFile)) == false)
    {
        std::cerr << "Error: selected rendering algorithm " 
                  << algorithm 
                  << " does not know how to render files of type " 
                  << vox::DataSetReader::GetFileExtension(inputFile) 
                  << std::endl;
        std::cout << "Hit Enter to exit..." << std::endl;
        char key;
        std::cin.get(key);
        std::cin.clear();
        return 1;
    }
    
    spRenderer->setRenderingGLContext(pGlCtx);
    spRenderer->setBackgroundGLContext(glWindow.getGLWidget().getBackgroundContext());
    spRenderer->setEnableLighting(noLighting == false);
    spRenderer->setup();
    
    spDataSet->setRenderer(spRenderer.get());

    glWindow.getGLWidget().addSceneObject(spDataSet.get());

    glWindow.show();

    int retVal = app.exec();

    spRenderer->shutdown();
    return retVal;
}
