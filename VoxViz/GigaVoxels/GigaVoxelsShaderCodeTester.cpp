#include "GigaVoxels/GigaVoxelsShaderCodeTester.h"

#include "VoxVizOpenGL/GLExtensions.h"
#include "VoxVizOpenGL/GLUtils.h"

//#include <glm/core/setup.hpp>
#define GLM_SWIZZLE GLM_SWIZZLE_FULL
#include <glm/glm.hpp>

#include <iostream>

using namespace glm;

#define uniform static
#define in
#define out
#define inout
#define discard return

static glm::vec4 gl_Position;

//vertex shader uniforms
uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;
uniform vec3 VolTranslation;
uniform vec3 VolScale;

static void GigaVoxelsVertexShader(const glm::vec4& glVertex, glm::vec4& RayPosition)
{
    gl_Position = ProjectionMatrix * ModelViewMatrix * glVertex;
    
    vec3 rayPosition = glVertex.xyz - VolTranslation;
    rayPosition /= VolScale;
    rayPosition += 0.5f;
    RayPosition = vec4(rayPosition, 1);
}

//#version 330
//TODO deal with samplers
//TODO set all uniforms

struct usampler3D
{
    usampler3D() : w(0), h(0), d(0), data(NULL) {}
    int w;
    int h;
    int d;
    glm::uvec4* data;
};

typedef usampler3D usampler2DArray;

struct sampler3D
{
    sampler3D() : w(0), h(0), d(0), data(NULL) {}
    int w;
    int h;
    int d;
    glm::vec4* data;
};

struct usampler2D
{
    usampler2D() : w(0), h(0), data(NULL) {}
    int w;
    int h;
    glm::uvec4* data;
};

struct usamplerPyramid2D
{
    usamplerPyramid2D() : numLODs(0) {}
    int numLODs;
    std::vector<usampler2D> mipMaps;
};

struct sampler1D
{
    sampler1D() : w(0), data(NULL) {}
    int w;
    glm::vec4* data;
};

static glm::uvec4 texelFetch(usampler3D& sampler,
                             const glm::ivec3& texCoord,
                             int)
{
    if(texCoord.z >= sampler.d ||
       texCoord.y >= sampler.h ||
       texCoord.x >= sampler.w)
    {
        return glm::uvec4(0u, 0u, 0u, 0u);
    }
    return sampler.data[(texCoord.z * sampler.h * sampler.w) 
                         + (texCoord.y * sampler.w) 
                         + texCoord.x];
}

static glm::uvec4 texelFetchOffset(usampler3D& sampler,
                             const glm::ivec3& texCoord,
                             int lod,
                             const ivec3& offset)
{
    if(texCoord.z + offset.z >= sampler.d ||
       texCoord.y + offset.y >= sampler.h ||
       texCoord.x + offset.x >= sampler.w)
    {
        return glm::uvec4(0u, 0u, 0u, 0u);
    }
    return sampler.data[((texCoord.z + offset.z) * sampler.h * sampler.w) 
                         + ((texCoord.y + offset.y) * sampler.w) 
                         + (texCoord.x + offset.x)];
}

static glm::uvec4 texelFetch(usampler2D& sampler,
                             const glm::ivec2& texCoord,
                             int)
{
    if(texCoord.y >= sampler.h ||
       texCoord.x >= sampler.w)
    {
        return glm::uvec4(0u, 0u, 0u, 0u);
    }
    return sampler.data[(texCoord.y * sampler.w) 
                         + texCoord.x];
}

static glm::uvec4 texelFetch(usamplerPyramid2D& sampler,
                             const glm::ivec2& texCoord,
                             int lod)
{
    return sampler.mipMaps.at(lod).data[(texCoord.y * sampler.mipMaps.at(lod).w) 
                                        + texCoord.x];
}

static glm::vec4 texture(sampler3D& sampler,
                         const glm::vec3& texCoord)
{
    int x = (int)(std::floor((static_cast<float>(sampler.w-1) * texCoord.x) + 0.5f));
    int y = (int)(std::floor((static_cast<float>(sampler.h-1) * texCoord.y) + 0.5f));
    int z = (int)(std::floor((static_cast<float>(sampler.d-1) * texCoord.z) + 0.5f));

    if(x >= sampler.w)
        x = sampler.w-1;
    if(y >= sampler.h)
        y = sampler.h-1;
    if(z >= sampler.d)
        z = sampler.d-1;
    return sampler.data[(z * sampler.w * sampler.h) 
                         + (y * sampler.w) 
                         + x];
}

static glm::vec4 textureOffset(sampler3D& sampler,
                               const glm::vec3& texCoord,
                               const glm::ivec3& offset)
{
    int x = (int)(std::floor((static_cast<float>(sampler.w-1) * texCoord.x) + 0.5f));
    int y = (int)(std::floor((static_cast<float>(sampler.h-1) * texCoord.y) + 0.5f));
    int z = (int)(std::floor((static_cast<float>(sampler.d-1) * texCoord.z) + 0.5f));

    x += offset.x;
    y += offset.y;
    z += offset.z;

    if(x < 0)
        x = 0;
    else if(x >= sampler.w)
        x = sampler.w-1;

    if(y < 0)
        y = 0;
    else if(y >= sampler.h)
        y = sampler.h-1;
    if(z < 0)
        z = 0;
    else if(z >= sampler.d)
        z = sampler.d-1;

    return sampler.data[(z * sampler.w * sampler.h) 
                         + (y * sampler.w) 
                         + x];
}

static glm::vec4 texture(sampler1D& sampler,
                         const float texCoord)
{
    int x = (int)(std::floor((static_cast<float>(sampler.w-1) * texCoord) + 0.5f));

    return sampler.data[x];
}

uniform usampler3D OctTreeSampler;
uniform sampler3D BrickSampler;
uniform sampler3D BrickGradientsSampler;
//uniform sampler2D ReadColorSampler;
uniform float RayStepSize; //step size of ray in world coordinates
uniform vec3 BrickStepVector; //step size of ray in brick pool
//uniform float BrickStepSize; //step size of ray in brick voxels
//uniform float ParentBrickStepSize; //step size of ray in parent brick voxels
//uniform vec4 NearPlane;//near plane
//uniform float NearPlaneDistance;//distance from camera to near plane
uniform vec3 CameraPosition;
uniform vec3 CameraUp;
uniform vec3 CameraLeft;
uniform vec3 LightPosition;
uniform vec3 VolExtentMin;
uniform vec3 VolExtentMax;
uniform vec3 BrickDimension;//number of texels in a brick
uniform float RootVoxelHalfSize;//half size of voxel in root node
uniform vec3 BrickPoolDimension;//number of texels in the brick pool
uniform float PixelSize;//size of a screen pixel
uniform float OctTreeDepthMinusOne;
uniform int MaxTreeTraversals;//max number of tree traversals allowed per ray
uniform int MaxNodesToPushThisFrame;//max number of nodes to push onto the node usage list this frame
uniform bool RootNodeIsConstant;
uniform bool GradientsAreUnsigned;

const float k_AlmostZero = 0.000001f;
const float k_TreeN = 2.0f;//Our tree is 2^3 (i.e. OctTree)
const float k_ChildNodeSizeScalar = 0.5f;//as the tree is descended the l/w/h of the node is halved
const float k_BrickStepSizeScalar = 2.0f;//as the tree is descended the ray step size changes

const int k_LeftPlane = 0;
const int k_BottomPlane = 1;
const int k_FrontPlane = 2;
const int k_RightPlane = 3;
const int k_TopPlane = 4;
const int k_BackPlane = 5;
//unit cube planes
vec4 NodePlanes[6] = //vec4[](
{
    //left
    vec4(-1.0f, 0.0f, 0.0f, 0.0f),
    //bottom
    vec4(0.0f, -1.0f, 0.0f, 0.0f),
    //front
    vec4(0.0f, 0.0f, -1.0f, 0.0f),
    //right
    vec4(1.0f, 0.0f, 0.0f, -1.0f),
    //top
    vec4(0.0f, 1.0f, 0.0f, -1.0f),
    //back
    vec4(0.0f, 0.0f, 1.0f, -1.0f)
};
//);

const int k_PlaneD = 3;

bool NodeIsContant(uvec4 octTreeNode)
{
    glm::uint nodeTypeBits = octTreeNode.r;

    return (nodeTypeBits & 0x40000000u) != 0u;
}

uvec3 GetChildNodePointer(uvec4 octTreeNode)
{
    glm::uint childNodePtrBits = octTreeNode.r;
    
    return uvec3(((childNodePtrBits >> 20u) & 0x000003FFu),
                ((childNodePtrBits >> 10u) & 0x000003FFu),
                (childNodePtrBits & 0x000003FFu));
}

bool BrickIsLoaded(uvec4 octTreeNode)
{
    glm::uint brickIsLoadedBits = octTreeNode.r;

    return (brickIsLoadedBits & 0x80000000u) != 0u;
}

vec3 GetBrickPointer(uvec4 octTreeNode)
{
    glm::uint brickPtrBits = octTreeNode.g;

    glm::uint uiS = (brickPtrBits >> 20u) & 0x000003FFu;
    glm::uint uiT = (brickPtrBits >> 10u) & 0x000003FFu;
    glm::uint uiR = brickPtrBits & 0x000003FFu;

    return vec3(float(uiS) / (BrickPoolDimension.x), 
                float(uiT) / (BrickPoolDimension.y), 
                float(uiR) / (BrickPoolDimension.z));
}

bool NodeIsConstant(uvec4 octTreeNode)
{
    glm::uint nodeTypeFlagBits = octTreeNode.r;

	return (nodeTypeFlagBits & 0x40000000u) != 0u;
}

vec4 GetConstantValue(uvec4 octTreeNode)
{
    glm::uint constValueBits = octTreeNode.g;

    glm::uint red = constValueBits & 0x000000FFu;
    constValueBits >>= 8u;
    glm::uint green = constValueBits & 0x000000FFu;

    constValueBits >>= 8u;
    glm::uint blue = constValueBits & 0x000000FFu;
    
    constValueBits >>= 8u;
    glm::uint alpha = constValueBits & 0x000000FFu;

	return vec4(float(red), 
                float(green), 
                float(blue), 
                float(alpha)) / 255.0f;
}

void ComputeMinRayPlaneIntersection(vec3 rayPos,
                                    vec3 rayDir,
                                    vec3 pointOnPlane,
                                    vec4 plane,
                                    inout float& tMin)
{
    float dirDotN = dot(rayDir, glm::vec3(plane.xyz));
    //only care about intersections with planes that point away from ray
    //because we only want the min time from ray start to exit of volume
    //planes that point toward the ray start position are either behind
    //the ray or they are automatically closer to the ray given that the 
    //planes form a rectangular cube
    if(dirDotN > 0)
    {
        vec3 vecFromRayStartToPlane = rayPos - pointOnPlane;
        float tIsect = - (dot(glm::vec3(plane.xyz), vecFromRayStartToPlane)) / dirDotN;
        if(tIsect < tMin)
        {
            //if the intersection time is zero and the 
            //ray is nearly parallel to the plane 
            //(i.e. dirDotN is almost zero) then use
            //the next smallest time
            if(abs(tIsect) > 0.0f)// || dirDotN > k_AlmostZero)
                tMin = tIsect;
        }
    }
}

float ComputeMinRayBoxIntersection(vec3 rayPos, 
                                vec3 rayDir,
                                vec3 nodeExtentMin,
                                vec3 nodeExtentMax)
{
    //update the plane D on each node's plane
    NodePlanes[k_LeftPlane][k_PlaneD] = nodeExtentMin.x;
    NodePlanes[k_BottomPlane][k_PlaneD] = nodeExtentMin.y;
    NodePlanes[k_FrontPlane][k_PlaneD] = nodeExtentMin.z;

    NodePlanes[k_RightPlane][k_PlaneD] = -nodeExtentMax.x;
    NodePlanes[k_TopPlane][k_PlaneD] = -nodeExtentMax.y;
    NodePlanes[k_BackPlane][k_PlaneD] = -nodeExtentMax.z;

    float tMin = 4.0;//volume should be a unit cube so no time greater than sqrt(3)

    for(int i = 0; 
        i < 6;
        ++i)
    {
        ComputeMinRayPlaneIntersection(rayPos, 
                                       rayDir,
                                       i < 3 ? nodeExtentMin : nodeExtentMax,
                                       NodePlanes[i],
                                       tMin);
    }

    return tMin;
}

float ComputeDistFromPlane(vec4 plane, vec3 point)
{
    return (plane[0] * point.x)
            + (plane[1] * point.y)
            + (plane[2] * point.z)
            + plane[3];
}

void AdvanceRay(inout vec3& rayWorldPosition,
                vec3 rayDir,
                float distToNodeBoundary,
                float rayStepSize)
{
    int numSteps = int(distToNodeBoundary / rayStepSize) + 1;
    rayWorldPosition += (rayDir * (numSteps * rayStepSize));
}

//vec4 TraverseConstant(vec4 constValue,
//                      inout vec3 rayWorldPosition, 
//                      vec3 rayDir,
//                      float rayStepSize,
//                      vec3 nodeExtentMin,
//                      vec3 nodeExtentMax,
//                      vec4 nodePlanes[6],
//                      vec4 dst)
//{
//    int isectPlane = 0;
//    float distToExit = ComputeMinRayBoxIntersection(rayWorldPosition, rayDir, 
//                                                    nodeExtentMin, 
//                                                    nodeExtentMax, 
//                                                    nodePlanes);
//
//    AdvanceRay(rayWorldPosition, rayDir, distToExit, rayStepSize);
// 
//    if(constValue.a < 0.005f)//constant is almost completely transparent so skip it
//        return dst;
//
//    float sampleRatio = distToExit / rayStepSize;
//
//    constValue.a = 1.0f - pow((1.0f - constValue.a), sampleRatio);
//
//    constValue.rgb *= sampleRatio;
//
//    dst = (1.0f - dst.a) * constValue + dst;
//    
//    return dst;
//}

void TraverseConstant(vec4 constValue,
                   inout vec3& rayWorldPosition,
                   vec3 rayDir,
                   float rayStepSize,
                   float sampleRatio,
                   vec3 nodeExtentMin,
                   vec3 nodeExtentMax,
                   inout vec4& dst)
{
    if(constValue.a < 0.005f)//constant is almost completely transparent so skip it
    {
        float timeToExit = ComputeMinRayBoxIntersection(rayWorldPosition, rayDir, 
                                                        nodeExtentMin, nodeExtentMax);

        //advance the ray
        AdvanceRay(rayWorldPosition, rayDir, timeToExit, rayStepSize);
        return;
    }

    vec3 worldRayStep = rayDir * rayStepSize;
    vec4 src;
    //float time = 0.0;
    //while(time <= timeToExit)
    vec3 test1, test2;
    float inside = 3.0f;
    while(inside >= 3.0f)
    {
        rayWorldPosition += worldRayStep;

        test1 = sign(rayWorldPosition - nodeExtentMin);
        test2 = sign(nodeExtentMax - rayWorldPosition);
        inside = dot(test1, test2); 

        //advance time
        //time += rayStepSize;

        src = constValue;

        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= glm::vec3(sampleRatio);

        dst = (1.0f - dst.a) * src + dst;
        /*if(dst.a > 0.995f)
        {
            while(inside >= 3.0f)
            {
                rayWorldPosition += (rayDir * rayStepSize);

                test1 = sign(rayWorldPosition - nodeExtentMin);
                test2 = sign(nodeExtentMax - rayWorldPosition);
                inside = dot(test1, test2); 
            }
            return;
        }*/
    }
}

// lighting factors
const vec3 k_Ambient = vec3(0.05, 0.05, 0.05);
const vec3 k_Diffuse = vec3(1.0, 1.0, 1.0);
const vec3 k_Specular = vec3(1.0, 1.0, 1.0);
const float k_Shininess = 100.0;

vec3 shading(vec3 rgb, vec3 normal, vec3 toCamera, vec3 toLight)
{
    vec3 halfway = normalize(toLight + toCamera);

    float diffuseLight = max(dot(toLight, normal), 0.0f);
    vec3 diffuse = vec3(0, 0, 0);
    vec3 specular = vec3(0, 0, 0);

    if(diffuseLight > 0.0)
    {
        diffuse = k_Diffuse * rgb * diffuseLight;

        float specularLight = pow(max(dot(halfway, normal), 0.0f), k_Shininess); 
        specular = k_Specular * rgb * specularLight;
    }

    vec3 ambient = k_Ambient * rgb;
        
    return ambient + diffuse + specular;
}

vec3 LookupGradient(vec3 curBrickRayPos)
{
    vec3 normal = texture(BrickGradientsSampler, curBrickRayPos).rgb;
    
    if(GradientsAreUnsigned)
    {
        normal *= 2.0f;
        normal -= 1.0f;
    }

    return normal;
}

void TraverseBrick(inout vec3& rayWorldPosition,
                   vec3 rayDir,
                   vec3 worldRayStep,
                   vec3 curBrickRayPos,
                   vec3 brickRayStep,
                   float sampleRatio,
                   vec3 nodeExtentMin,
                   vec3 nodeExtentMax,
                   inout vec4& dst)
{
    vec3 lightDir = normalize(LightPosition - rayWorldPosition);

    vec3 test1, test2;
    float inside = 3.0f;
    while(inside >= 3.0f)
    {

        vec4 src = texture(BrickSampler, curBrickRayPos);

        vec3 normal = LookupGradient(curBrickRayPos);
        //advance cur ray position
        curBrickRayPos += brickRayStep;
        //advance world space ray
        rayWorldPosition += worldRayStep;

        test1 = sign(rayWorldPosition - nodeExtentMin);
        test2 = sign(nodeExtentMax - rayWorldPosition);
        inside = dot(test1, test2); 

        //compute color
        
        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= glm::vec3(sampleRatio);

        normal = normalize(normal);
        //not all voxels will have valid gradient which indicates homogeneous region 
        //in which case lighting should not be applied
        //if(isnan(normal) != true)
        //{
        //    src.rgb += shading(src.rgb, 
        //                       normal, 
        //                       -rayDir, //TODO these should change as ray progresses
        //                       lightDir);
        //}

        dst = (1.0f - dst.a) * src + dst;
        if(dst.a > 0.995f)
            break;
    }
    if(dst.a > 1.0f)
    {
        float scalar = 1.0f / dst.a;
        dst *= scalar;
    }
}

void TraverseBrickInterp(inout vec3& rayWorldPosition,
                         vec3 rayDir,
                         vec3 worldRayStep,
                         vec3 curBrickRayPos,
                         vec3 curBrickRayStep,
                         vec3 prevBrickRayPos,
                         vec3 prevBrickRayStep,
                         float interp,
                         float sampleRatio,
                         vec3 nodeExtentMin,
                         vec3 nodeExtentMax,
                         bool prevBrickIsConstant,
                         vec4 prevBrickConstValue,
                         inout vec4& dst)
{
    float oneMinusInterp = 1.0f - interp;
    vec3 lightDir = normalize(LightPosition - rayWorldPosition);

    vec3 test1, test2;
    float inside = 3.0f;
    while(inside >= 3.0f)
    {
        vec4 src = texture(BrickSampler, curBrickRayPos);

        vec3 normal = LookupGradient(curBrickRayPos);
        //{ 
        src *= oneMinusInterp;
        normal *= oneMinusInterp;
        if(!prevBrickIsConstant)
        {
            vec4 prevVoxel = texture(BrickSampler, prevBrickRayPos);
            src += (prevVoxel * interp);

            vec3 prevGrad = LookupGradient(prevBrickRayPos);
            normal += (prevGrad * interp);

            prevBrickRayPos += prevBrickRayStep;
        }
        else
            src += (prevBrickConstValue * interp);
        
            //advance prev brick position;
        //}

        //advance cur ray position
        curBrickRayPos += curBrickRayStep;
        //advance world space ray
        rayWorldPosition += worldRayStep;

        test1 = sign(rayWorldPosition - nodeExtentMin);
        test2 = sign(nodeExtentMax - rayWorldPosition);
        inside = dot(test1, test2); 

        //compute color
        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= glm::vec3(sampleRatio);

        normal = normalize(normal);
        //not all voxels will have valid gradient which indicates homogeneous region 
        //in which case lighting should not be applied
        //if(isnan(normal) != true)
        {
            src.rgb += shading(src.rgb, 
                               normal, 
                               -rayDir, //TODO these should change as ray progresses
                               lightDir);
        }

        dst = (1.0f - dst.a) * src + dst;
    }
}

//float ComputeVoxelSize(float distToNearPlane, float nodeSize)
float ComputeVoxelSize(vec3 rayPosition, float voxelHalfSize)
{
    vec3 voxLL = (rayPosition - (CameraUp * voxelHalfSize) + (CameraLeft * voxelHalfSize));
    vec3 camToLL = normalize(voxLL - CameraPosition);
    vec3 voxUR = (rayPosition + (CameraUp * voxelHalfSize) - (CameraLeft * voxelHalfSize));
    vec3 camToUR = normalize(voxUR - CameraPosition);
    float dotProd = dot(camToLL, camToUR);
    return 1.0 - dotProd;
    //compute voxel size
    //if(distToNearPlane > 0.0f)//if less than zero then near plane is inside bounds of node
    //    return (nodeSize / MinBrickDimension) / distToNearPlane;//* (NearPlaneDistance / distFromNodeToNearPlane);
    //else
    //    return PixelSize;
}

//layout(pixel_center_integer) in ivec4 gl_FragCoord;

in vec4 RayPosition;
out vec4 FragColor;
out uvec4 NodeUsageList[3];

const int k_NumNodeLists = 3;
const int k_NumComponentsPerList = 4;
const int k_MaxNodesPerPixel = 12;//3 lists times 4 components (rgba)

void AddToNodeUsageList(inout int& curListIndex, 
                        inout int& curComponentIndex,
                        uvec4 nodeUsage)
{
    glm::uint bits = 0u;
    //for some reason this 30u or-ing causes corruption of the list
    //don't think i really need
    //bits |= (nodeUsage.w << 30u);
    //now x, y, z
    bits |= (nodeUsage.x << 20u);
    bits |= (nodeUsage.y << 10u);
    bits |= nodeUsage.z;

    NodeUsageList[curListIndex][curComponentIndex] = bits;

    //uvec4 check = uvec4(0u, 0u, 0u, nodeUsage.w);
    //check.x = (bits >> 20u) & 0x000003FFu;
    //check.y = (bits >> 10u) & 0x000003FFu;
    //check.z = bits & 0x000003FFu;

    ++curComponentIndex;
    if(curComponentIndex == k_NumComponentsPerList)
    {
        curComponentIndex = 0;
        ++curListIndex;
        if(curListIndex == k_NumNodeLists)
            curListIndex = 0;
    }

    //return (check == nodeUsage);
}

//layout(pixel_center_integer) in ivec4 gl_FragCoord;

static void GigaVoxelsFragmentShader(const in ivec4& gl_FragCoord,
                                     const in vec4& RayPosition)
{
    NodeUsageList[0] = uvec4(0u, 0u, 0u, 0u);
    NodeUsageList[1] = uvec4(0u, 0u, 0u, 0u);
    NodeUsageList[2] = uvec4(0u, 0u, 0u, 0u);

    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);

    vec4 dst = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    //vec4 dst = texelFetch(ReadColorSampler, pixelCoord, 0);
    //if(dst.a > 0.995f)
    //{
    //    FragColor = dst;
        //return;
    //}

    vec3 rayWorldPosition = RayPosition.xyz;
    vec3 rayDir = normalize(rayWorldPosition - CameraPosition);

    vec3 nextPosition = rayWorldPosition + (rayDir * RayStepSize * 0.01f);
    vec3 test1 = sign(nextPosition - VolExtentMin);
    vec3 test2 = sign(VolExtentMax - nextPosition);
    float inside = dot(test1, test2);
    if(inside >= 3.0f)
    {
        //outer traversal variables
        int curListIndex = 0;
        int curComponentIndex = 0;
        uvec4 nodeUsage = uvec4(0u);
    
        pixelCoord %= 2;
    
        int startPushingNodes = (pixelCoord.x * k_MaxNodesPerPixel) 
                                 + (pixelCoord.y * k_MaxNodesPerPixel * 2);
        int stopPushingNodes = startPushingNodes + k_MaxNodesPerPixel;
        
        int maxNodesPerPixelBlock = k_MaxNodesPerPixel * 2 * 2; //pixel blocks ar 2x2
    
        vec3 brickRayStep = rayDir;
        brickRayStep *= BrickStepVector;
        vec3 prevBrickRayStep = brickRayStep;
        //the texels in the parent brick are the same physical distance apart,
        //but represent twice as much space... actually parent may not be only one level higher
        prevBrickRayStep *= 0.5f;

        //inner traversal variables
        ivec3 octTreeNodePtr = ivec3(0);//start at root
        uvec4 rootOctTreeNode = texelFetch(OctTreeSampler, octTreeNodePtr, 0);
        uvec4 octTreeNode = rootOctTreeNode;

        vec3 nodeExtentMin = VolExtentMin;
        vec3 nodeExtentMax = VolExtentMax;
        float nodeSize = 1.0f;
        float voxelHalfSize = RootVoxelHalfSize;
        
        vec3 rayBrickPosition = rayWorldPosition;//position of ray relative to current octree node's brick
        vec3 curBrickRayPosition = rayWorldPosition;//position of ray relative to current brick (most recently encounted brick during descent)
        vec3 prevBrickRayPosition = rayWorldPosition;//position of ray relative to previous brick (second most recently encounted brick during descent)
            
        uvec4 curBrickPtr = rootOctTreeNode;
        uvec4 prevBrickPtr = curBrickPtr;
        vec4 curConstValue = vec4(0.0f);
        bool brickIsLoaded = false;
        bool curNodeIsConstant = RootNodeIsConstant;
        bool prevNodeIsConstant = false;
        float projVoxelSize = 0.0f;
        //float curProjVoxelSize = 0.0f;//TODO, I don't need these anymore
        //float prevProjVoxelSize = 0.0f;
        int curDepth = 0;
        int curBrickDepth = 0;
        uvec3 childNodePtr = uvec3(0u);
        //float distToNearPlane = ComputeDistFromPlane(NearPlane, rayWorldPosition);
    
        //int traversalCount = 0;
        int nodeListTraversalCount = 0;
        int pushedNodeCount = 0;
        //bool traversedCorrectNode = false;
        while(inside >= 3.0f)
        {
            //brickIsLoaded = BrickIsLoaded(octTreeNode);
            
            curNodeIsConstant = NodeIsConstant(octTreeNode);
            if(curNodeIsConstant)
            {
                curConstValue = GetConstantValue(octTreeNode);
            }
            else 
            {
                prevNodeIsConstant = curNodeIsConstant;
    
                brickIsLoaded = BrickIsLoaded(octTreeNode);
    
                if(brickIsLoaded)
                {
                    //only update brick data if brick is loaded on GPU
                    prevBrickPtr = curBrickPtr;
    
                    curBrickPtr = octTreeNode;
    
                    prevBrickRayPosition = curBrickRayPosition;
    
                    curBrickRayPosition = rayBrickPosition;
    
                    curBrickDepth = curDepth;
                }
            }

            float parentVoxelSize = projVoxelSize;
            projVoxelSize = ComputeVoxelSize(rayWorldPosition, //distToNearPlane,
                                             voxelHalfSize);
    
            if(//PixelSize > projVoxelSize //projected voxel size is less than pixel size
                curDepth == 0
               || (childNodePtr = GetChildNodePointer(octTreeNode)) == uvec3(0))//no children means its leaf
            {
                //mark this node as being used for rendering
                nodeUsage = uvec4(octTreeNodePtr, glm::uint(brickIsLoaded));
    
                if(curNodeIsConstant)
                    //the or in this if statement is so special case that i'm eliminating it for optimization
                   //|| (RootNodeIsConstant && curBrickPtr == vec3(0.0f)))
                {
                    //every time we decend down the tree the step size halves
                    float sampleRatio = pow(k_BrickStepSizeScalar, 
                                            OctTreeDepthMinusOne - curDepth);
    
                    TraverseConstant(curConstValue,
                                     rayWorldPosition, 
                                     rayDir,
                                     RayStepSize * sampleRatio,
                                     sampleRatio,
                                     nodeExtentMin,
                                     nodeExtentMax,
                                     dst);
                }
                else
                {
                    //advances the ray position
                    //traverses the most recently found brick
                    vec3 curBrickRayPos = GetBrickPointer(curBrickPtr)
                                            + ((curBrickRayPosition * BrickDimension) 
                                            / BrickPoolDimension);
                    float sampleRatio = pow(k_BrickStepSizeScalar,
                                            OctTreeDepthMinusOne - curBrickDepth);
                    vec3 worldRayStep = rayDir;
                    worldRayStep *= RayStepSize;
                    worldRayStep *= sampleRatio;

                    float interpolation = 1.0f;
                    //curBrickStepSize is the step size for the current brick in world coordinates
                    if(PixelSize > projVoxelSize &&
                       curDepth != 0 &&
                       ((interpolation = clamp(((PixelSize - projVoxelSize) / (parentVoxelSize - projVoxelSize)), 0.0f, 1.0f)) > 0.001f))
                    {
                        vec3 prevBrickRayPos = GetBrickPointer(prevBrickPtr)
                                                + ((prevBrickRayPosition * BrickDimension)
                                                   / BrickPoolDimension);
                        
                        TraverseBrickInterp(rayWorldPosition,
                                            rayDir,
                                            worldRayStep,
                                            curBrickRayPos,
                                            brickRayStep,
                                            prevBrickRayPos,
                                            prevBrickRayStep,
                                            interpolation,
                                            sampleRatio,
                                            nodeExtentMin, //curBrickNodeExtentMin,
                                            nodeExtentMax, //curBrickNodeExtentMax,
                                            prevNodeIsConstant,
                                            curConstValue,
                                            dst);
                    }
                    else
                    {
                        TraverseBrick(rayWorldPosition,
                                      rayDir,
                                      worldRayStep,
                                      curBrickRayPos,
                                      brickRayStep,
                                      sampleRatio,
                                      nodeExtentMin, //curBrickNodeExtentMin,
                                      nodeExtentMax, //curBrickNodeExtentMax,
                                      dst);
                    }
                }

                if(dst.a > 0.995f && (curNodeIsConstant || brickIsLoaded))
                //only cut this traversal short if the current node's brick or constant value was traversed
                {
                    break;
                    //FragColor = dst;
                    //return;
                }
                
                octTreeNode = rootOctTreeNode;
                nodeExtentMin = VolExtentMin;
                nodeExtentMax = VolExtentMax;
                nodeSize = 1.0f;
                voxelHalfSize = RootVoxelHalfSize;

                rayBrickPosition = rayWorldPosition;//position of ray relative to current octree node's brick
                curBrickRayPosition = rayWorldPosition;//position of ray relative to current brick (most recently encounted brick during descent)
                prevBrickRayPosition = rayWorldPosition;//position of ray relative to previous brick (second most recently encounted brick during descent)
        
                curBrickPtr = octTreeNode;
                prevBrickPtr = curBrickPtr;
                curConstValue = vec4(0.0f);
                brickIsLoaded = false;
                curNodeIsConstant = RootNodeIsConstant;
                prevNodeIsConstant = false;
                projVoxelSize = 0.0f;
                //curProjVoxelSize = 0.0f;//TODO, I don't need these anymore
                //prevProjVoxelSize = 0.0f;
                curDepth = 0;
                curBrickDepth = 0;
                childNodePtr = uvec3(0u);
                //distToNearPlane = ComputeDistFromPlane(NearPlane, rayWorldPosition);
            
                if(nodeListTraversalCount >= startPushingNodes
                   && nodeListTraversalCount < stopPushingNodes
                   && pushedNodeCount < MaxNodesToPushThisFrame)
                {
                    ++pushedNodeCount;

                    AddToNodeUsageList(curListIndex, curComponentIndex, nodeUsage);
                }

                /*if(traversalCount > MaxTreeTraversals)
                {
                    //dst = vec4(rayPosition.z, 0, 0, 0.25);
                    FragColor = vec4(1, 0, 1, 1);
                    return;
                }*/
        
                test1 = sign(rayWorldPosition - VolExtentMin);
                test2 = sign(VolExtentMax - rayWorldPosition);
                inside = dot(test1, test2);

                //++traversalCount;
                ++nodeListTraversalCount;

                if(nodeListTraversalCount >= maxNodesPerPixelBlock)
                    nodeListTraversalCount = 0;//start over
            }
            else
            { 
                //every time we descend further down the tree the
                //size of the brick texels decrease
                vec3 brickOffset = rayBrickPosition * k_TreeN;
    
                uvec3 childOffset = clamp(uvec3(brickOffset), 0u, 1u);
    
                //update brick position and step size
                rayBrickPosition = brickOffset - vec3(childOffset);
    
                //update node ptr and size
                octTreeNodePtr = ivec3(childNodePtr + childOffset);
    
                ++curDepth;
                //as we descend down the tree the node size decreases
                nodeSize *= k_ChildNodeSizeScalar;
                //update the extents
                nodeExtentMin += (vec3(childOffset) * nodeSize);
                nodeExtentMax -= ((vec3(1.0f) - vec3(childOffset)) * nodeSize);

                voxelHalfSize *= k_ChildNodeSizeScalar;
            
                octTreeNode = texelFetch(OctTreeSampler, octTreeNodePtr, 0);
            }
        }        
    }

    FragColor = dst;
}

void gv::GigaVoxelsShaderCodeTester::drawVolume(voxOpenGL::ShaderProgram* pShaderProgram,
                                                GigaVoxelsOctTree* pOctTree,
                                                const vox::FloatArray& vertexArray,
                                                float lookX,
                                                float lookY,
                                                float lookZ)
{
    pShaderProgram->getUniformValue("ProjectionMatrix", &ProjectionMatrix[0][0]);
    
    pShaderProgram->getUniformValue("VolTranslation", &VolTranslation[0]);
    //std::cout << VolTranslation.x << ", " << VolTranslation.y << ", " << VolTranslation.z << std::endl;

    pShaderProgram->getUniformValue("VolScale", &VolScale[0]);

    std::vector<glm::vec4> rayPositions;
    for(vox::FloatArray::const_iterator vertItr = vertexArray.begin();
        vertItr != vertexArray.end();
        ++vertItr)
    {
        float x = *vertItr;
        ++vertItr;
        float y = *vertItr;
        ++vertItr;
        float z = *vertItr;

        glm::vec4 glVertex(x, y, z, 1.0f); 
        rayPositions.resize(rayPositions.size()+1);

        GigaVoxelsVertexShader(glVertex, rayPositions.back());
    }

    glActiveTexture(GL_TEXTURE0);
    
    if(OctTreeSampler.data == NULL)
    {
        glBindTexture(GL_TEXTURE_3D, pOctTree->getTreeTextureID());
    
        OctTreeSampler.data = (glm::uvec4*)
            voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_3D,
                                                            OctTreeSampler.w,
                                                            OctTreeSampler.h,
                                                            OctTreeSampler.d,
                                                            GL_RGBA_INTEGER, 
                                                            GL_UNSIGNED_INT);

        //static glm::vec4 brickData[1] = { glm::vec4(1.0, 0.0, 0.0, 1.0) };
        //BrickSampler.data = &brickData[0];
        //BrickSampler.w = BrickSampler.h = BrickSampler.d = 1;
        glBindTexture(GL_TEXTURE_3D, pOctTree->getBrickTextureID());

        BrickSampler.data = 
            (glm::vec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_3D,
                                                                        BrickSampler.w,
                                                                        BrickSampler.h,
                                                                        BrickSampler.d,
                                                                        GL_RGBA, GL_FLOAT);

        struct rgba
        {
            float r;
            float g;
            float b;
            float a;
        };
        rgba* bricks = new rgba[BrickSampler.w * BrickSampler.h * BrickSampler.d];
        memcpy(bricks, BrickSampler.data, sizeof(rgba) * BrickSampler.w * BrickSampler.h * BrickSampler.d);
        //glBindTexture(GL_TEXTURE_3D, pOctTree->getBrickGradientTextureID());

        static glm::vec4 brickGradData[1] = { glm::vec4(0.0, 0.0, 0.0, 0.0f) };
        BrickGradientsSampler.data = &brickGradData[0];
        BrickGradientsSampler.w = BrickGradientsSampler.h = BrickGradientsSampler.d = 1;
        //BrickGradientsSampler.data = 
        //    (glm::vec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_3D,
        //                                                                BrickGradientsSampler.w,
        //                                                                BrickGradientsSampler.h,
        //                                                                BrickGradientsSampler.d,
        //                                                                GL_RGBA, GL_FLOAT);

        /*rgba* brickGrads = new rgba[BrickGradientsSampler.w * BrickGradientsSampler.h * BrickGradientsSampler.d];
        memcpy(brickGrads, BrickGradientsSampler.data, sizeof(rgba) * BrickGradientsSampler.w * BrickGradientsSampler.h * BrickGradientsSampler.d);*/

        delete bricks;
        //delete brickGrads;
    }

    pShaderProgram->getUniformValue("RayStepSize", &RayStepSize);
    pShaderProgram->getUniformValue("BrickStepVector", &BrickStepVector[0]);
    pShaderProgram->getUniformValue("OctTreeDepthMinusOne", &OctTreeDepthMinusOne);
    //pShaderProgram->getUniformValue("BrickStepSize", &BrickStepSize);
    //pShaderProgram->getUniformValue("ParentBrickStepSize", &ParentBrickStepSize);
    //pShaderProgram->getUniformValue("NearPlane", &NearPlane[0]);
    //pShaderProgram->getUniformValue("NearPlaneDistance", &NearPlaneDistance);
    pShaderProgram->getUniformValue("CameraPosition", &CameraPosition[0]);
    pShaderProgram->getUniformValue("CameraUp", &CameraUp[0]);
    pShaderProgram->getUniformValue("CameraLeft", &CameraLeft[0]);
    pShaderProgram->getUniformValue("LightPosition", &LightPosition[0]);
    pShaderProgram->getUniformValue("VolExtentMin", &VolExtentMin[0]);
    pShaderProgram->getUniformValue("VolExtentMax", &VolExtentMax[0]);
    pShaderProgram->getUniformValue("BrickDimension", &BrickDimension[0]);
    //pShaderProgram->getUniformValue("MinBrickDimension", &MinBrickDimension);
    pShaderProgram->getUniformValue("RootVoxelHalfSize", &RootVoxelHalfSize);
    pShaderProgram->getUniformValue("BrickPoolDimension", &BrickPoolDimension[0]);
    pShaderProgram->getUniformValue("PixelSize", &PixelSize);
    pShaderProgram->getUniformValue("MaxTreeTraversals", &MaxTreeTraversals);
    pShaderProgram->getUniformValue("MaxNodesToPushThisFrame", &MaxNodesToPushThisFrame);
    pShaderProgram->getUniformValue("RootNodeIsConstant", RootNodeIsConstant);
    pShaderProgram->getUniformValue("GradientsAreUnsigned", GradientsAreUnsigned);
    //pShaderProgram->getUniformValue("OctTreeDepth", &OctTreeDepth);

    float scale = 1.0f/44.0f;
    int pX = 224;
    for(float x = 0.49f; x >= 0.0f; x -= 0.01f, pX -= 1)
    {
        int pY = 224;
        for(float y = 0.49f; y > 0.20f; y -= 0.01f, pY -= 1)
        {
            GigaVoxelsFragmentShader(ivec4(pX, pY, 0, 0),  /*vec4(x, y, 0.0, 1.0)*/vec4(CameraPosition.x + (lookX * scale), 
                                                                CameraPosition.y + (lookY * scale),  
                                                                CameraPosition.z + (lookZ * scale), 1.0));
        }
    }
}

uniform usampler2DArray NodeUsageListSampler;
uniform vec2 NodeUsageListDimMinusTwo;

//const int k_NumNodeLists = 3;
//const int k_NumComponentsPerList = 4;
const int k_NumNodeListItems = 12;

static void GenerateSelectionMask(const in ivec4& gl_FragCoord,
                                  out uvec4& Output)
{
    //convert tex coord to 2D array integer indices
    //ivec2 texel = ivec2(TexCoord * NodeUsageListDimMinusTwo);
    ivec3 texel = ivec3(gl_FragCoord.xy, 0);
 
    //save the pixel/texel position, store 0 at red for now
    Output = uvec4(0u, texel.x, texel.y, 1u);

    //offsets for lists 2 and 3 
    const ivec3 list1 = ivec3(0, 0, 1);
    const ivec3 list2 = ivec3(0, 0, 2);

    uvec4 nodeUsageList[k_NumNodeLists] = {//uvec4[k_NumNodeLists](
        texelFetch(NodeUsageListSampler, texel, 0),
        texelFetch(NodeUsageListSampler, texel + list1, 0),
        texelFetch(NodeUsageListSampler, texel + list2, 0)
    };//);

    if(nodeUsageList[0][0] == 0u)
        return;//if first item in my list is zero then this is an empty node usage list
      
    //we sample the neighbors two pixels away because each 2x2 block
    //of pixels record a different range encountered nodes so the pixel
    //that is two pixels away records the same range of pixels as our
    //current pixel and is thus most likely to contain duplicates
    uvec4 topNodeUsageList[k_NumNodeLists];
    uvec4 topRightNodeUsageList[k_NumNodeLists];
    uvec4 rightNodeUsageList[k_NumNodeLists];
    if(texel.y < NodeUsageListDimMinusTwo.y)
    {
        ivec3 neighbor = ivec3(texel.x, texel.y + 2, 0);

        uvec4 _topNodeUsageList[] = {//uvec4[k_NumNodeLists](
            texelFetch(NodeUsageListSampler, neighbor, 0),
            texelFetch(NodeUsageListSampler, neighbor + list1, 0),
            texelFetch(NodeUsageListSampler, neighbor + list2, 0)
        };//);
        memcpy(topNodeUsageList, _topNodeUsageList, sizeof(topNodeUsageList));

        if(texel.x < NodeUsageListDimMinusTwo.x)
        {
            //sample to right
            neighbor.x += 2;

            uvec4 _topRightNodeUsageList[] = {//uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            };//);
            memcpy(topRightNodeUsageList, _topRightNodeUsageList, sizeof(topRightNodeUsageList));
    
            neighbor.y = texel.y;

            uvec4 _rightNodeUsageList[] = {//uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            };//);
            memcpy(rightNodeUsageList, _rightNodeUsageList, sizeof(rightNodeUsageList));
        }
        else
        {
            memset(topRightNodeUsageList, 0, sizeof(topRightNodeUsageList));// = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
            memset(rightNodeUsageList, 0, sizeof(rightNodeUsageList));// = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        }
    }
    else
    {
        memset(topNodeUsageList, 0, sizeof(topNodeUsageList));// = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        memset(topRightNodeUsageList, 0, sizeof(topRightNodeUsageList));// = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        if(texel.x < NodeUsageListDimMinusTwo.x)
        {
            ivec3 neighbor = ivec3(texel.x + 2, texel.y, 0);

            uvec4 _rightNodeUsageList[] = {//uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            };//);
            memcpy(rightNodeUsageList, _rightNodeUsageList, sizeof(rightNodeUsageList));
        }
        else
            memset(rightNodeUsageList, 0, sizeof(rightNodeUsageList));// = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
    }

    int prevListIndex;
    int curListIndex = k_NumNodeLists-1;
    int nextListIndex = 0;

    int prevCompIndex;
    int curCompIndex = k_NumComponentsPerList-1;
    int nextCompIndex = 0;

    glm::uint nodeUsage;
    for(int nodeListIndex = 0; nodeListIndex < k_NumNodeListItems; ++nodeListIndex)
    {
        prevListIndex = curListIndex;
        prevCompIndex = curCompIndex;

        curListIndex = nextListIndex;
        curCompIndex = nextCompIndex,

        ++nextCompIndex;

        if(nextCompIndex == k_NumComponentsPerList)
        {
            nextCompIndex = 0;
            ++nextListIndex;
            if(nextListIndex == k_NumNodeLists)
            {
                nextListIndex = 0;
            }
        }

        nodeUsage = nodeUsageList[curListIndex][curCompIndex];
            
        if(nodeUsage == 0u)
            return;//if zero then we've hit the end of this list, so we can quit

        size_t x, y, z;

        x = (nodeUsage >> 20u) & 0x000003FFu;
        y = (nodeUsage >> 10u) & 0x000003FFu;
        z = nodeUsage & 0x000003FFu;
        //now look at i-1, i, and i+1 items
        //in the neighbor lists (top, top-right, and right)
        //if my neighbor contains the same node as me then
        //don't keep my item
        //if(texel.y < NodeUsageListDimMinusTwo.y)
        {
            //sample same item in list
            if(nodeUsage == topNodeUsageList[curListIndex][curCompIndex])
                continue;

            //sample prev item in list
            if(nodeUsage == topNodeUsageList[prevListIndex][prevCompIndex])
                continue;
            
            //sample next item in list
            if(nodeUsage == topNodeUsageList[nextListIndex][nextCompIndex])
                continue;

            //if(texel.x < NodeUsageListDimMinusTwo.x)
            {
                //sample to top-right
                //sample same item in list
                if(nodeUsage == topRightNodeUsageList[curListIndex][curCompIndex])
                    continue;
                
                //sample prev item in list
                if(nodeUsage == topRightNodeUsageList[prevListIndex][prevCompIndex])
                    continue;
                
                //sample next item in list
                if(nodeUsage == topRightNodeUsageList[nextListIndex][nextCompIndex])
                    continue;
            }
        }

        //if(texel.x < NodeUsageListDimMinusTwo.x)
        {
            //sample to right
            //sample same item in list
            if(nodeUsage == rightNodeUsageList[curListIndex][curCompIndex])
                continue;
            
            //sample prev item in list
            if(nodeUsage == rightNodeUsageList[prevListIndex][prevCompIndex])
                continue;

            //sample next item in list
            if(nodeUsage == rightNodeUsageList[nextListIndex][nextCompIndex])
                continue;
        }

        //if we get here then the node in the current list does
        //not match any of the other lists, so save it in the output
        //keep this node
        Output.r |= (1u << glm::uint(nodeListIndex));
    }
}

void gv::GigaVoxelsShaderCodeTester::generateNodeUsageSelectionMask(voxOpenGL::ShaderProgram* pShaderProgram,
                                                                    unsigned int nodeUsageTexture)
{
    pShaderProgram->getUniformValue("NodeUsageListDimMinusTwo", &NodeUsageListDimMinusTwo[0]);

    glActiveTexture(GL_TEXTURE0);

    if(NodeUsageListSampler.data != NULL)
        delete [] NodeUsageListSampler.data;

    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, nodeUsageTexture);

        NodeUsageListSampler.data = (glm::uvec4*)
            voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D_ARRAY,
                                                            NodeUsageListSampler.w,
                                                            NodeUsageListSampler.h,
                                                            NodeUsageListSampler.d,
                                                            GL_RGBA_INTEGER, 
                                                            GL_UNSIGNED_INT);

        GLint y = NodeUsageListSampler.h >> 1;
        GLint x = NodeUsageListSampler.w >> 1;
        GLint endY = (NodeUsageListSampler.h >> 1)+1;
        GLint endX = (NodeUsageListSampler.w >> 1)+5;

        for( ; y < endY; ++y)
        {
            for( ; x < endX; ++x)
            {
                std::cout << "NodeUsage at " << x << " " << y << std::endl;
                for(GLint z = 0; z < NodeUsageListSampler.d; ++z)
                {
                    glm::uvec4 nodeUsage = 
                        NodeUsageListSampler.data[(z * NodeUsageListSampler.h * NodeUsageListSampler.w) + (y * NodeUsageListSampler.w) + x];
                    for(GLint i = 0; i < 4; ++i)
                    {
                        unsigned int nodeID = nodeUsage[i];
                        if(nodeID != 0)
                        {
                            size_t x, y, z;

                            x = (nodeID >> 20u) & 0x000003FFu;
                            y = (nodeID >> 10u) & 0x000003FFu;
                            z = nodeID & 0x000003FFu;

                            std::cout << x << " " << y << " " << z << std::endl;
                        }
                    }
                }
                std::cout << std::endl;
            }
        }

        struct urgba
        {
            unsigned r;
            unsigned g;
            unsigned b;
            unsigned a;
        };

        urgba* test = new urgba[NodeUsageListSampler.w * NodeUsageListSampler.h * NodeUsageListSampler.d];
        memcpy(test, NodeUsageListSampler.data, sizeof(urgba) * NodeUsageListSampler.w * NodeUsageListSampler.h * NodeUsageListSampler.d);

        urgba* test2 = new urgba[NodeUsageListSampler.w * NodeUsageListSampler.h];
        memcpy(test2, NodeUsageListSampler.data, sizeof(urgba) * NodeUsageListSampler.w * NodeUsageListSampler.h);

        urgba* test3 = new urgba[NodeUsageListSampler.w * NodeUsageListSampler.h];
        memcpy(test3, 
               &NodeUsageListSampler.data[(1 * NodeUsageListSampler.h * NodeUsageListSampler.w)], 
               sizeof(urgba) * NodeUsageListSampler.w * NodeUsageListSampler.h);

        urgba* test4 = new urgba[NodeUsageListSampler.w * NodeUsageListSampler.h];
        memcpy(test4, 
               &NodeUsageListSampler.data[(2 * NodeUsageListSampler.h * NodeUsageListSampler.w)], 
               sizeof(urgba) * NodeUsageListSampler.w * NodeUsageListSampler.h);


        delete [] test;
        delete [] test2;
        delete [] test3;
        delete [] test4;
    }

    uvec4* output = new uvec4[NodeUsageListSampler.w * NodeUsageListSampler.h];

    for(int y = 0; y < NodeUsageListSampler.h; ++y)
    {
        for(int x = 0; x < NodeUsageListSampler.w; ++x)
        {
            ivec4 pixel(x, y, 0, 0);
            uvec4& outputPixel = output[(y * NodeUsageListSampler.w) + x];
            
            if(x == 400 && y == 300)
                x = 400;
            GenerateSelectionMask(pixel,
                                  outputPixel);
        }
    }

    delete [] output;
}

uniform usampler2D SelectionMaskSampler;

static void ComputeActiveTexels(const in ivec4& gl_FragCoord,
                                out uvec4& Output)
{
    ivec2 texCoord = ivec2(gl_FragCoord.xy);

    //red component contains a bit vector, set bits indicate
    //that an item in the corresponding node list should be kept
    //if there are not set bits then this pixel is not active
    //this will be true for any pixels/rays that did not hit the volume
    //or for pixels that had the same nodes in their list as its neighbor
    //pixels (see GenerateSelectionMask.frag)
    glm::uint selectionBits = texelFetch(SelectionMaskSampler, texCoord, 0).r;

    if(selectionBits == 0u)
        Output = uvec4(0u, 0u, 0u, 0u);
    else
        Output = uvec4(1u, 0u, 0u, 1u);
}

void gv::GigaVoxelsShaderCodeTester::computeActiveTexels(unsigned int selectionMaskTextureID)
{
    glActiveTexture(GL_TEXTURE0);

    if(SelectionMaskSampler.data == NULL)
    {
        glBindTexture(GL_TEXTURE_2D,
                      selectionMaskTextureID);

        GLint depth;
        SelectionMaskSampler.data = 
            (glm::uvec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D,
                                                                         SelectionMaskSampler.w,
                                                                         SelectionMaskSampler.h,
                                                                         depth,
                                                                         GL_RGBA_INTEGER, 
                                                                         GL_UNSIGNED_INT);

        struct urgba
        {
            unsigned r;
            unsigned g;
            unsigned b;
            unsigned a;
        };

        urgba* test = new urgba[SelectionMaskSampler.w * SelectionMaskSampler.h];

        memcpy(test, SelectionMaskSampler.data, 
               sizeof(urgba) * SelectionMaskSampler.w * SelectionMaskSampler.h);

        delete [] test;
    }

    uvec4* output = new uvec4[SelectionMaskSampler.w * SelectionMaskSampler.h];

    for(int y = 0; y < SelectionMaskSampler.h; ++y)
    {
        for(int x = 0; x < SelectionMaskSampler.w; ++x)
        {
            ivec4 pixel(x, y, 0, 0);
            
            uvec4& outputPixel = output[(y * SelectionMaskSampler.w) + x];
            
            if(x == 400 && y == 300)
                x = 400;
            ComputeActiveTexels(pixel,
                                outputPixel);
        }
    }

    delete [] output;
}

uniform usampler2D InputTextureSampler;

static void GenerateSelectionMaskHistoPyramid(const in ivec4& gl_FragCoord,
                                              out uvec4& Output)
{
    //the viewport for this fragment shader should be 
    //half the size of the InputTextureSampler's texture
    ivec2 texCoord = ivec2(gl_FragCoord.xy * 2);
    
    //for each pixel in the output image we
    //compute the sum of the four texels in 
    //the higher res mipmap image
    glm::uint texelValue = texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.x += 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.y += 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
   
    texCoord.x -= 1;
    texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;

    texCoord.y += 1;
    if(texCoord.y == InputTextureSampler.h-1)
    {
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.x += 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.x += 1;
        if(texCoord.x == InputTextureSampler.w-1)
        {
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
            texCoord.y -= 1;
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
            texCoord.y -= 1;
            texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        }
    }
    else if((texCoord.x += 2) == InputTextureSampler.w-1)
    {
        texCoord.y -= 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
        texCoord.y -= 1;
        texelValue += texelFetch(InputTextureSampler, texCoord, 0).r;
    }
    
    Output = uvec4(texelValue, 0u, 0u, 1u);
}

void gv::GigaVoxelsShaderCodeTester::generateSelectionMaskHistoPyramid(int renderTargetWidth,
                                                                       int renderTargetHeight,
                                                                       unsigned int histoPyramidTextureID,
                                                                       int histoPyramidTextureLevel)
{
    glActiveTexture(GL_TEXTURE0);

    if(InputTextureSampler.data == NULL)
    {
        glBindTexture(GL_TEXTURE_2D,
                      histoPyramidTextureID);

        GLint depth;
        InputTextureSampler.data = 
            (glm::uvec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D,
                                                                         InputTextureSampler.w,
                                                                         InputTextureSampler.h,
                                                                         depth,
                                                                         GL_RGBA_INTEGER, 
                                                                         GL_UNSIGNED_INT,
                                                                         histoPyramidTextureLevel);

        struct urgba
        {
            unsigned r;
            unsigned g;
            unsigned b;
            unsigned a;
        };

        urgba* test = new urgba[InputTextureSampler.w * InputTextureSampler.h];

        memcpy(test, InputTextureSampler.data, 
               sizeof(urgba) * InputTextureSampler.w * InputTextureSampler.h);

        delete [] test;
    }

    uvec4* output = new uvec4[renderTargetWidth * renderTargetHeight];

    for(int y = 0; y < renderTargetHeight; ++y)
    {
        for(int x = 0; x < renderTargetWidth; ++x)
        {
            ivec4 pixel(x, y, 0, 0);
            uvec4& outputPixel = output[(y * renderTargetWidth) + x];
            if(x == 200 && y == 150)
                x = 200;
            GenerateSelectionMaskHistoPyramid(pixel,
                                              outputPixel);
        }
    }

    delete [] output;

    delete [] InputTextureSampler.data;
    InputTextureSampler.data = NULL;
}

uniform usamplerPyramid2D SelectionMaskHistoPyramidSampler;
uniform int SelectionMaskHistoPyramidLevelCount;
uniform int CompressedSelectionMaskWidth;
uniform glm::uint CompressedSelectionMaskMaxIndex;
uniform int MaxUNodesToPushThisFrame;

const ivec2 k_SameLevelTraversalIncr[9] = //ivec2[](
{
    //defines how to walk through a 4x4 block of texels starting at (x,y)
    ivec2(1, 0), //go to (x+1, y)
    ivec2(-1, 1),//next go to (x, y+1)
    ivec2(1, 0),//go to(x+1, y+1)
    //for odd dimensions keep going
    ivec2(-1, 1),//go to(x, y+2)
    ivec2(1, 0),//go to(x+1, y+2)
    ivec2(1, 0),//go to(x+2, y+2)
    ivec2(0, -1),//go to(x+2, y+1)
    ivec2(0, -1),//go to(x+2, y)
    ivec2(0, 0)
};
//);

//uniform usampler2DArray NodeUsageListSampler;
//uniform usampler2D SelectionMaskSampler;

//const int k_NumNodeLists = 3;
//const int k_NumComponentsPerList = 4;
const int k_NumSelectionMaskBits = 12; //k_NumNodeLists * k_NumComponentsPerList;
const ivec2 k_NodeListIndices[12] = {//ivec2[12](
    ivec2(0, 0), ivec2(0, 1), ivec2(0, 2), ivec2(0, 3),
    ivec2(1, 0), ivec2(1, 1), ivec2(1, 2), ivec2(1, 3),
    ivec2(2, 0), ivec2(2, 1), ivec2(2, 2), ivec2(2, 3)
};//);
const int k_OutputIndices[12] = {//int[12](
    0, 1, 2, 3, 
    0, 1, 2, 3,
    0, 1, 2, 3
};//);
//out uvec4 UniqueNodeUsage;

void CompressNodeUsageList(const in ivec4& gl_FragCoord,
                           out uvec4& UniqueNodeUsage)
{
    glm::uint key = (gl_FragCoord.y * CompressedSelectionMaskWidth) + gl_FragCoord.x;

    if(key > CompressedSelectionMaskMaxIndex)
    {
        //UniqueNodeUsage = uvec4(0u, 0u, 0u, 0u);
        //return;
        discard;
    }
    
    glm::uint start = 0u;
    ivec2 texCoord = ivec2(0, 0);
    ivec2 texSize = ivec2(1, 1);
    //start at lowest detail mip map level
    int pyramidLevel = SelectionMaskHistoPyramidLevelCount - 1;
    int curLevelLoopCount = 0;

    glm::uint end = 0u;
    while(pyramidLevel >= 0)
    {
        end = start + texelFetch(SelectionMaskHistoPyramidSampler, texCoord, pyramidLevel).r;

        if(key >= start && key < end)
        {
            --pyramidLevel;//descend a level
            curLevelLoopCount = 0;
            texCoord <<= 1;
            if(pyramidLevel >= 0)
                texSize = ivec2(SelectionMaskHistoPyramidSampler.mipMaps.at(pyramidLevel).w, 
                                SelectionMaskHistoPyramidSampler.mipMaps.at(pyramidLevel).h);
        }
        else
        {
            texCoord += k_SameLevelTraversalIncr[curLevelLoopCount];
            ++curLevelLoopCount;
            if(curLevelLoopCount == 4 && texCoord.y != texSize.y-1)
            {
                //y+2 is not the last row so jump to 
                //x+2 column
                curLevelLoopCount = 7;
                texCoord.x += 2;
                texCoord.y -= 1;
            }
            
            if(curLevelLoopCount >= 6 && texCoord.x != texSize.x-1)
            {
                //x+2 is not last column so exit
                curLevelLoopCount = 9;
            }

            start = end;
            if(curLevelLoopCount == 9)
            {
                //i'm pretty sure this should never happen, but need way out to
                //prevent endless loop
                UniqueNodeUsage = uvec4(0u, 0u, 0u, 1u);
                return;
            }
        }
    }

    texCoord >>= 1;
    if(texCoord.x == 400 && texCoord.y == 300)
        std::cout << "here" << std::endl;
    //use the final tex coord computed in the loop above to
    //lookup the selection mask bits
    uvec4 selectionMaskTexel = texelFetch(SelectionMaskSampler, texCoord, 0);

    glm::uint selectionMaskBits = selectionMaskTexel.r;
    ivec3 nodeUsageListTexCoord = ivec3(selectionMaskTexel.g, selectionMaskTexel.b, 0);

    //offsets for lists 2 and 3 
    const ivec3 list1 = ivec3(0, 0, 1);
    const ivec3 list2 = ivec3(0, 0, 2);
    uvec4 nodeUsageList[3] = {//uvec4[](
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord, 0),
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord + list1, 0),
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord + list2, 0)
    };//);

    uvec4 nodeListItems = uvec4(0u, 0u, 0u, 0u);
    int pushedNodes = 0;
    //for each set bit in the selection mask grab the corresponding
    //item from the node usage list and store in the unique node usage output
    for(int itemIndex = 0; 
        itemIndex < k_NumSelectionMaskBits 
        && selectionMaskBits != 0u
        && pushedNodes < MaxUNodesToPushThisFrame;//k_NumComponentsPerList;//as soon as we have stored 4 then quit
        ++itemIndex, selectionMaskBits >>= 1u)
    {
        glm::uint testBit = selectionMaskBits & 1u;
        if(testBit != 0u)
        {
            ivec2 indices = k_NodeListIndices[itemIndex];
            glm::uint nodeListItem = nodeUsageList[indices.x][indices.y];

            nodeListItems[k_OutputIndices[pushedNodes]] = nodeListItem;

            ++pushedNodes;
        }
    }

    UniqueNodeUsage = nodeListItems;
}

bool VerifyHistoPyramid(int pyramidLevel, const glm::ivec2& texCoord)
{
    if(pyramidLevel == 0)
        return true;

    glm::uint texelValue = texelFetch(SelectionMaskHistoPyramidSampler, texCoord, pyramidLevel).r;

    glm::ivec2 nextLevelUV1 = texCoord << 1;
    --pyramidLevel;

    glm::uint texelSum = texelFetch(SelectionMaskHistoPyramidSampler, nextLevelUV1, pyramidLevel).r;
   
    ivec2 nextLevelUV2 = nextLevelUV1;
    nextLevelUV2.x += 1;
    texelSum += texelFetch(SelectionMaskHistoPyramidSampler, nextLevelUV2, pyramidLevel).r;
    
    ivec2 nextLevelUV3 = nextLevelUV2;
    nextLevelUV3.y += 1;
    texelSum += texelFetch(SelectionMaskHistoPyramidSampler, nextLevelUV3, pyramidLevel).r;

    ivec2 nextLevelUV4 = nextLevelUV3;
    nextLevelUV4.x -= 1;
    texelSum += texelFetch(SelectionMaskHistoPyramidSampler, nextLevelUV4, pyramidLevel).r;

    ivec2 extraCoord = nextLevelUV4;
    extraCoord.y += 1;
    if(extraCoord.y == SelectionMaskHistoPyramidSampler.mipMaps.at(pyramidLevel).h-1)
    {
        texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
        extraCoord.x += 1;
        texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
        extraCoord.x += 1;
        if(extraCoord.x == SelectionMaskHistoPyramidSampler.mipMaps.at(pyramidLevel).w-1)
        {
            texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
            extraCoord.y -= 1;
            texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
            extraCoord.y -= 1;
            texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
        }
    }
    else if((extraCoord.x += 2) == SelectionMaskHistoPyramidSampler.mipMaps.at(pyramidLevel).w-1)
    {
        extraCoord.y -= 1;
        texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
        extraCoord.y -= 1;
        texelSum += texelFetch(SelectionMaskHistoPyramidSampler, extraCoord, pyramidLevel).r;
    }
    
    VerifyHistoPyramid(pyramidLevel, nextLevelUV1);
    VerifyHistoPyramid(pyramidLevel, nextLevelUV2);
    VerifyHistoPyramid(pyramidLevel, nextLevelUV3);
    VerifyHistoPyramid(pyramidLevel, nextLevelUV4);

    if(texelValue != texelSum)
        return false;

    return true;
}

void gv::GigaVoxelsShaderCodeTester::compressNodeUsageList(voxOpenGL::ShaderProgram* pShaderProgram,
                                                           int renderTargetWidth,
                                                           int renderTargetHeight,
                                                           unsigned int histoPyramidTextureID,
                                                           unsigned int nodeUsageTexture,
                                                           unsigned int selectionMaskTexture)
{
    pShaderProgram->getUniformValue("SelectionMaskHistoPyramidLevelCount",
                                    &SelectionMaskHistoPyramidLevelCount);
    pShaderProgram->getUniformValue("CompressedSelectionMaskWidth",
                                    &CompressedSelectionMaskWidth);
    pShaderProgram->getUniformValue("CompressedSelectionMaskMaxIndex",
                                    &CompressedSelectionMaskMaxIndex);
    pShaderProgram->getUniformValue("MaxNodesToPushThisFrame",
                                    &MaxUNodesToPushThisFrame);

    glActiveTexture(GL_TEXTURE0);

    
    if(SelectionMaskHistoPyramidSampler.mipMaps.size() == 0)
    {
        glBindTexture(GL_TEXTURE_2D,
                      histoPyramidTextureID);

        GLint depth;

        SelectionMaskHistoPyramidSampler.mipMaps.resize(SelectionMaskHistoPyramidLevelCount);

        for(int i = 0; i < SelectionMaskHistoPyramidLevelCount; ++i)
        {
            SelectionMaskHistoPyramidSampler.mipMaps[i].data = 
                (glm::uvec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D,
                                                                             SelectionMaskHistoPyramidSampler.mipMaps[i].w,
                                                                             SelectionMaskHistoPyramidSampler.mipMaps[i].h,
                                                                             depth,
                                                                             GL_RGBA_INTEGER, 
                                                                             GL_UNSIGNED_INT,
                                                                             i);
        }

        glm::ivec2 texCoord(0, 0);
        VerifyHistoPyramid(SelectionMaskHistoPyramidLevelCount-1, texCoord);
    }

    if(NodeUsageListSampler.data == NULL)
    {
        glBindTexture(GL_TEXTURE_2D_ARRAY, nodeUsageTexture);

        NodeUsageListSampler.data = (glm::uvec4*)
            voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D_ARRAY,
                                                            NodeUsageListSampler.w,
                                                            NodeUsageListSampler.h,
                                                            NodeUsageListSampler.d,
                                                            GL_RGBA_INTEGER, 
                                                            GL_UNSIGNED_INT);

        struct urgba
        {
            unsigned r;
            unsigned g;
            unsigned b;
            unsigned a;
        };

        urgba* test = new urgba[NodeUsageListSampler.w * NodeUsageListSampler.h * NodeUsageListSampler.d];
        memcpy(test, NodeUsageListSampler.data, sizeof(urgba) * NodeUsageListSampler.w * NodeUsageListSampler.h * NodeUsageListSampler.d);

        delete [] test;
    }

    if(SelectionMaskSampler.data == NULL)
    {
        glBindTexture(GL_TEXTURE_2D,
                      selectionMaskTexture);

        GLint depth;
        SelectionMaskSampler.data = 
            (glm::uvec4*)voxOpenGL::GLUtils::LoadImageFromCurrentTexture(GL_TEXTURE_2D,
                                                                         SelectionMaskSampler.w,
                                                                         SelectionMaskSampler.h,
                                                                         depth,
                                                                         GL_RGBA_INTEGER, 
                                                                         GL_UNSIGNED_INT);
       
        //urgba* test = new urgba[SelectionMaskSampler.w * SelectionMaskSampler.h];

        //memcpy(test, SelectionMaskSampler.data, 
        //       sizeof(urgba) * SelectionMaskSampler.w * SelectionMaskSampler.h);

        //delete [] test;
    }

    struct URGBA
    {
        unsigned r;
        unsigned g;
        unsigned b;
        unsigned a;
    };
    URGBA* output = new URGBA[renderTargetWidth * renderTargetHeight];

    for(int y = 0; y < renderTargetHeight; ++y)
    {
        for(int x = 0; x < renderTargetWidth; ++x)
        {
            ivec4 pixel(x, y, 0, 0);
            uvec4& outputPixel = *reinterpret_cast<uvec4*>(&output[(y * renderTargetWidth) + x]);
            CompressNodeUsageList(pixel,
                                  outputPixel);
        }
    }

    delete [] output;
}
