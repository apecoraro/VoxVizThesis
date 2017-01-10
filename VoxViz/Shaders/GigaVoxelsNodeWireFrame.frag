#version 330

uniform usampler3D OctTreeSampler;
uniform sampler3D BrickSampler;
uniform sampler3D BrickGradientsSampler;
uniform sampler2D JitterTexSampler;
uniform int JitterTexSize;
uniform float RayStepSize; //step size of ray in world coordinates
uniform vec3 BrickStepVector; //step size of ray in brick pool
//uniform float ParentBrickStepSize; //half size of step in cur brick voxels
//uniform vec4 NearPlane;//near plane
//uniform float NearPlaneDistance;//distance from camera to near plane
uniform vec3 CameraPosition;
uniform vec3 CameraUp;
uniform vec3 CameraLeft;
uniform vec3 LightPosition;
uniform vec3 VolExtentMin;
uniform vec3 VolExtentMax;
uniform vec3 BrickDimension;//number of texels in a brick
uniform float RootVoxelHalfSize;//size of voxel in root node
uniform vec3 BrickPoolDimension;//number of texels in the brick pool
uniform float PixelSize;//size of a screen pixel
uniform int OctTreeDepthMinusOne;//tree depth minus one
uniform int MaxTreeTraversals;//max number of tree traversals allowed per ray
uniform int MaxNodesToPushThisFrame;//max number of nodes to push onto the node usage list this frame
uniform bool RootNodeIsConstant;
uniform bool GradientsAreUnsigned;//gradients are mapped to 0 to 1 and need to be mapped back to -1 to 1
uniform bool ShowConstantNodes;
uniform bool ComputeLighting;
uniform float LodScalar;

const float k_AboutZero = 0.002f;
//const float k_AboutOne = 0.99f;
//const float k_AlmostZero = 0.000001f;
const float k_TreeN = 2.0f;//Our tree is 2^3 (i.e. OctTree)
const float k_ChildNodeSizeScalar = 0.5f;//as the tree is descended the l/w/h of the node is halved
//const float k_BrickStepSizeScalar = 0.5f;//as the tree is descended the ray step size changes
const float k_BrickStepSizeScalar = 2.0f;//as the tree is descended the leaf brick step size doubles

const int k_LeftPlane = 0;
const int k_BottomPlane = 1;
const int k_FrontPlane = 2;
const int k_RightPlane = 3;
const int k_TopPlane = 4;
const int k_BackPlane = 5;
//unit cube planes
vec4 NodePlanes[6] = vec4[](
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
);

const int k_PlaneD = 3;

bool NodeIsContant(uvec4 octTreeNode)
{
    uint nodeTypeBits = octTreeNode.r;

    return (nodeTypeBits & 0x40000000u) != 0u;
}

uvec3 GetChildNodePointer(uvec4 octTreeNode)
{
    uint childNodePtrBits = octTreeNode.r;
    
    return uvec3(((childNodePtrBits >> 20u) & 0x000003FFu),
                ((childNodePtrBits >> 10u) & 0x000003FFu),
                (childNodePtrBits & 0x000003FFu));
}

bool BrickIsLoaded(uvec4 octTreeNode)
{
    uint brickIsLoadedBits = octTreeNode.r;

    return (brickIsLoadedBits & 0x80000000u) != 0u;
}

vec3 GetBrickPointer(uvec4 octTreeNode)
{
    uint brickPtrBits = octTreeNode.g;

    uint uiS = (brickPtrBits >> 20u) & 0x000003FFu;
    uint uiT = (brickPtrBits >> 10u) & 0x000003FFu;
    uint uiR = brickPtrBits & 0x000003FFu;

    return vec3(float(uiS) / (BrickPoolDimension.x), 
                float(uiT) / (BrickPoolDimension.y), 
                float(uiR) / (BrickPoolDimension.z));
}

bool NodeIsConstant(uvec4 octTreeNode)
{
	uint nodeTypeFlagBits = octTreeNode.r;

	return (nodeTypeFlagBits & 0x40000000u) != 0u;
}

vec4 GetConstantValue(uvec4 octTreeNode)
{
	uint constValueBits = octTreeNode.g;

    uint red = constValueBits & 0x000000FFu;
    constValueBits >>= 8u;
	uint green = constValueBits & 0x000000FFu;

    constValueBits >>= 8u;
	uint blue = constValueBits & 0x000000FFu;
    
    constValueBits >>= 8u;
	uint alpha = constValueBits & 0x000000FFu;

	return vec4(float(red), 
                float(green), 
                float(blue), 
                float(alpha)) / 255.0f;
}

void ComputeMinRayPlaneIntersection(vec3 rayPos,
                                    vec3 rayDir,
                                    vec3 pointOnPlane,
                                    vec4 plane,
                                    inout float tMin)
{
    float dirDotN = dot(rayDir, plane.xyz);
    //only care about intersections with planes that point away from ray
    //because we only want the min time from ray start to exit of volume
    //planes that point toward the ray start position are either behind
    //the ray or they are automatically closer to the ray given that the 
    //planes form a rectangular cube
    if(dirDotN > 0)
    {
        vec3 vecFromRayStartToPlane = rayPos - pointOnPlane;
        float tIsect = - (dot(plane.xyz, vecFromRayStartToPlane)) / dirDotN;
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

void AdvanceRay(inout vec3 rayWorldPosition,
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
                   inout vec3 rayWorldPosition,
                   vec3 rayDir,
                   float rayStepSize,
                   float sampleRatio,
                   vec3 nodeExtentMin,
                   vec3 nodeExtentMax,
                   inout vec4 dst)
{
    float timeToExit = ComputeMinRayBoxIntersection(rayWorldPosition, rayDir, 
                                                    nodeExtentMin, nodeExtentMax);
    if(constValue.a < 0.005f)//constant is almost completely transparent so skip it
    {

        //advance the ray
        AdvanceRay(rayWorldPosition, rayDir, timeToExit, rayStepSize);
        return;
    }

    int numSteps = int(timeToExit / (rayStepSize)) + 1;
    vec3 worldRayStep = rayDir * rayStepSize;
    vec4 src;
    //float time = 0.0;
    //while(time <= timeToExit)
    //vec3 test1, test2;
    //float inside = 3.0f;
    //while(inside >= 3.0f)
    for(int i = 0; i < numSteps; ++i)
    {
        rayWorldPosition += worldRayStep;

        //test1 = sign(rayWorldPosition - nodeExtentMin);
        //test2 = sign(nodeExtentMax - rayWorldPosition);
        //inside = dot(test1, test2); 

        if(dst.a > 0.995f)
        {
            numSteps -= (i+1);
            rayWorldPosition += (rayDir * (numSteps * rayStepSize));
            return;
        }

        //advance time
        //time += rayStepSize;

        src = constValue;

        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= sampleRatio;

        dst = (1.0f - dst.a) * src + dst;
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

    float diffuseLight = max(dot(toLight, normal), 0);
    vec3 diffuse = vec3(0, 0, 0);
    vec3 specular = vec3(0, 0, 0);

    if(diffuseLight > 0.0)
    {
        diffuse = k_Diffuse * rgb * diffuseLight;

        float specularLight = pow(max(dot(halfway, normal), 0), k_Shininess); 
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

void TraverseBrick(inout vec3 rayWorldPosition,
                   vec3 rayDir,
                   vec3 worldRayStep,
                   vec3 curBrickRayPos,
                   vec3 brickRayStep,
                   float sampleRatio,
                   vec3 nodeExtentMin,
                   vec3 nodeExtentMax,
                   inout vec4 dst)
{
    vec3 lightDir = normalize(LightPosition - rayWorldPosition);

    float timeToExit = ComputeMinRayBoxIntersection(rayWorldPosition, rayDir, 
                                                    nodeExtentMin, nodeExtentMax);
    float rayStepSize = (RayStepSize * sampleRatio);
    //advance the ray
    int numSteps = int(timeToExit / rayStepSize) + 1;
    //vec3 test1, test2;
    //float inside = 3.0f;
    sampleRatio = 1.0f;
    //while(inside >= 3.0f)
    for(int i = 0; i < numSteps; ++i)
    {
        
        //advance world space ray
        rayWorldPosition += worldRayStep;

        //test1 = sign(rayWorldPosition - nodeExtentMin);
        //test2 = sign(nodeExtentMax - rayWorldPosition);
        //inside = dot(test1, test2); 

        if(dst.a > 0.995f)
        {
            numSteps -= (i+1);
            rayWorldPosition += (rayDir * (numSteps * rayStepSize));
            return;
        }

        vec4 src = texture(BrickSampler, curBrickRayPos);
        
        //compute color
        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= sampleRatio;

        if(ComputeLighting)
		{
			vec3 normal = LookupGradient(curBrickRayPos);
        
			normal = normalize(normal);
			//not all voxels will have valid gradient which indicates homogeneous region 
			//in which case lighting should not be applied
			if(isnan(normal) != true)
			{
				src.rgb += shading(src.rgb, 
								   normal, 
								   -rayDir, //TODO these should change as ray progresses
								   lightDir);
			}
		}

        dst = (1.0f - dst.a) * src + dst;

		//advance cur ray position
        curBrickRayPos += brickRayStep;
    }
}

/*void TraverseBrickInterp(inout vec3 rayWorldPosition,
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
                         inout vec4 dst)
{
    float oneMinusInterp = 1.0f - interp;
    vec3 lightDir = normalize(LightPosition - rayWorldPosition);

    float timeToExit = ComputeMinRayBoxIntersection(rayWorldPosition, rayDir, 
                                                    nodeExtentMin, nodeExtentMax);

    float rayStepSize = (RayStepSize * sampleRatio);
    //advance the ray
    int numSteps = int(timeToExit / rayStepSize) + 1;
    sampleRatio = 1.0f;
    //vec3 test1, test2;
    //float inside = 3.0f;
    //while(inside >= 3.0f)
    for(int i = 0; i < numSteps; ++i)
    {
        //advance world space ray
        rayWorldPosition += worldRayStep;

        //test1 = sign(rayWorldPosition - nodeExtentMin);
        //test2 = sign(nodeExtentMax - rayWorldPosition);
        //inside = dot(test1, test2); 

        if(dst.a > 0.995f)
        {
            numSteps -= (i+1);
            rayWorldPosition += (rayDir * (numSteps * rayStepSize));
            return;
        }

        vec4 src = texture(BrickSampler, curBrickRayPos);

        src *= oneMinusInterp;

        if(!prevBrickIsConstant)
        {
            vec4 prevVoxel = texture(BrickSampler, prevBrickRayPos);
            src += (prevVoxel * interp);
        }
        else
            src += (prevBrickConstValue * interp);

        //compute color
        src.a = 1.0f - pow((1.0f - src.a), sampleRatio);
        src.rgb *= sampleRatio;

        if(ComputeLighting)
		{
			vec3 normal = LookupGradient(curBrickRayPos);
			normal *= oneMinusInterp;
			if(!prevBrickIsConstant)
			{
				vec3 prevGrad = LookupGradient(prevBrickRayPos);
				normal += (prevGrad * interp);
			}

			normal = normalize(normal);
			//not all voxels will have valid gradient which indicates homogeneous region 
			//in which case lighting should not be applied
			if(isnan(normal) != true)
			{
				src.rgb += shading(src.rgb, 
								   normal, 
								   -rayDir, //TODO these should change as ray progresses
								   lightDir);
			}
		}

        dst = (1.0f - dst.a) * src + dst;
		//advance cur ray position
        curBrickRayPos += curBrickRayStep;
		prevBrickRayPos += prevBrickRayStep;
    }
}*/

const vec4 k_NodeWireColors[7] = vec4[]( 
    vec4(0, 0, 0, 1),
    vec4(1, 1, 1, 1),
    vec4(0.5, 1, 1, 1),
    vec4(0, 1, 1, 1),
    vec4(0, 1, 0, 1),
    vec4(0, 0, 1, 1),
    vec4(1, 0, 1, 1)
);

vec4 GetNodeWireColor(int curDepth)
{
    return k_NodeWireColors[curDepth];
}

const vec4 k_ConstNodeWireColor = vec4(1, 0, 0, 1);

bool IsOnNodeBorder(vec3 rayWorldPosition,
                    vec3 nodeExtentMin,
                    vec3 nodeExtentMax)
{
    vec3 minDiff = rayWorldPosition - nodeExtentMin;
    vec3 maxDiff = nodeExtentMax - rayWorldPosition;
    //both x and y on min
    if(minDiff.x < k_AboutZero &&
       minDiff.y < k_AboutZero)
        return true;
    //both x and y on max
    if(maxDiff.x < k_AboutZero &&
       maxDiff.y < k_AboutZero)
        return true;
    //x near min y near max
    if(minDiff.x < k_AboutZero &&
       maxDiff.y < k_AboutZero)
        return true;
    //x near max y near min
    if(maxDiff.x < k_AboutZero &&
       minDiff.y < k_AboutZero)
        return true;
    //both z and y on min
    if(minDiff.z < k_AboutZero &&
       minDiff.y < k_AboutZero)
        return true;
    //both z and y on max
    if(maxDiff.z < k_AboutZero &&
       maxDiff.y < k_AboutZero)
        return true;
    //z near min y near max
    if(minDiff.z < k_AboutZero &&
       maxDiff.y < k_AboutZero)
        return true;
    //z near max y near min
    if(maxDiff.z < k_AboutZero &&
       minDiff.y < k_AboutZero)
        return true;
    //both z and x on min
    if(minDiff.z < k_AboutZero &&
       minDiff.x < k_AboutZero)
        return true;
    //both z and x on max
    if(maxDiff.z < k_AboutZero &&
       maxDiff.x < k_AboutZero)
        return true;
    //z near min x near max
    if(minDiff.z < k_AboutZero &&
       maxDiff.x < k_AboutZero)
        return true;
    //z near max x near min
    if(maxDiff.z < k_AboutZero &&
       minDiff.x < k_AboutZero)
        return true;
    return false;
}

//float ComputeVoxelSize(float distToNearPlane, float nodeSize)
float ComputeVoxelSize(vec3 rayPosition, float voxelHalfSize)
{
    vec3 camToLL = normalize((rayPosition - (CameraUp * voxelHalfSize) + (CameraLeft * voxelHalfSize)) -
                    CameraPosition);
    vec3 camToUR = normalize((rayPosition + (CameraUp * voxelHalfSize) - (CameraLeft * voxelHalfSize)) -
                    CameraPosition);
    return 1.0 - dot(camToLL, camToUR);
    //compute voxel size
    //if(distToNearPlane > 0.0f)//if less than zero then near plane is inside bounds of node
    //    return (nodeSize / MinBrickDimension) / distToNearPlane;//* (NearPlaneDistance / distFromNodeToNearPlane);
    //else
    //    return PixelSize;
}

layout(pixel_center_integer) in ivec4 gl_FragCoord;

in vec4 RayPosition;
out vec4 FragColor;
out uvec4 NodeUsageList[3];

const int k_NumNodeLists = 3;
const int k_NumComponentsPerList = 4;
const int k_MaxNodesPerPixel = 12;//3 lists times 4 components (rgba)
//const int k_MaxDepth = 1;

void AddToNodeUsageList(inout int curListIndex, 
                        inout int curComponentIndex,
                        uvec4 nodeUsage)
{
    uint bits = 0u;
    //for some reason this 30u or-ing causes corruption of the list
    //don't think i really need
    //bits |= (nodeUsage.w << 30u);
    //now x, y, z
    //add one so that the root node is not 0,0,0 - so
    //we can detect difference between no node usage 
    //and usage of only the root node
    nodeUsage += 1u;//we'll subtract it after reading it back to cpu (to get original id)
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

void main()
{
    NodeUsageList[0] = uvec4(0u, 0u, 0u, 0u);
    NodeUsageList[1] = uvec4(0u, 0u, 0u, 0u);
    NodeUsageList[2] = uvec4(0u, 0u, 0u, 0u);

    ivec2 pixelCoord = ivec2(gl_FragCoord.xy);

    vec4 dst = vec4(0.0f, 0.0f, 0.0f, 0.0f);

    vec3 rayWorldPosition = RayPosition.xyz;
    vec3 rayDir = normalize(rayWorldPosition - CameraPosition);

    ivec2 jitterTexCoord = pixelCoord % JitterTexSize;
    float jitterOffset = texelFetch(JitterTexSampler, jitterTexCoord, 0).r;
    rayWorldPosition += (rayDir * RayStepSize * jitterOffset);
    vec3 test1 = sign(rayWorldPosition - VolExtentMin);
    vec3 test2 = sign(VolExtentMax - rayWorldPosition);
    float inside = dot(test1, test2);
    int traversalCount = 0;
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
        //vec3 prevBrickRayStep = brickRayStep;
        //the texels in the parent brick are the same physical distance apart,
        //but represent twice as much space... actually parent may not be only one level higher
        //prevBrickRayStep *= 0.5f;

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
        //vec3 prevBrickRayPosition = rayWorldPosition;//position of ray relative to previous brick (second most recently encounted brick during descent)
            
        uvec4 curBrickPtr = rootOctTreeNode;
        //uvec4 prevBrickPtr = curBrickPtr;
        vec4 curConstValue = vec4(0.0f);
        bool brickIsLoaded = false;
        bool curNodeIsConstant = RootNodeIsConstant;
        //bool prevNodeIsConstant = false;
        float projVoxelSize = 0.0f;
        //float curProjVoxelSize = 0.0f;//TODO, I don't need these anymore
        //float prevProjVoxelSize = 0.0f;
        int curDepth = 0;
        int curBrickDepth = 0;
        uvec3 childNodePtr = uvec3(0u);
        //float distToNearPlane = ComputeDistFromPlane(NearPlane, rayWorldPosition);
    
        int nodeListTraversalCount = 0;
        int pushedNodeCount = 0;
        //bool traversedCorrectNode = false;
        while(inside >= 3.0f)
        //for(int i = 0; i < numSteps; ++i)
        {
            //prevNodeIsConstant = curNodeIsConstant;
            curNodeIsConstant = NodeIsConstant(octTreeNode);
    
            brickIsLoaded = BrickIsLoaded(octTreeNode);
    
            if(brickIsLoaded)
            {
                //only update brick data if brick is loaded on GPU
                //prevBrickPtr = curBrickPtr;
                curBrickPtr = octTreeNode;
                //prevBrickRayPosition = curBrickRayPosition;
                curBrickRayPosition = rayBrickPosition;
                curBrickDepth = curDepth;
            }

            float parentVoxelSize = projVoxelSize;
            projVoxelSize = ComputeVoxelSize(rayWorldPosition,
                                             voxelHalfSize) * LodScalar;
    

            if((!curNodeIsConstant && !brickIsLoaded)
                || PixelSize > projVoxelSize //projected voxel size is less than pixel size
               //curDepth == 4
                || (childNodePtr = GetChildNodePointer(octTreeNode)) == uvec3(0))//no children means its leaf
            {
                //mark this node as being used for rendering
                nodeUsage = uvec4(octTreeNodePtr, uint(brickIsLoaded));
    

                if(curNodeIsConstant)
                    //the or in this if statement is so special case that i'm eliminating it for optimization
                   //|| (RootNodeIsConstant && curBrickPtr == vec3(0.0f)))
                {
                    if(ShowConstantNodes && IsOnNodeBorder(rayWorldPosition, nodeExtentMin, nodeExtentMax))
                        dst += k_ConstNodeWireColor;
                    //every time we decend down the tree the step size halves
                    float sampleRatio = pow(k_BrickStepSizeScalar, 
                                            OctTreeDepthMinusOne - curDepth);

                    curConstValue = GetConstantValue(octTreeNode);
                    TraverseConstant(curConstValue,
                                     rayWorldPosition, 
                                     rayDir,
                                     RayStepSize * sampleRatio,
                                     sampleRatio,
                                     nodeExtentMin,
                                     nodeExtentMax,
                                     dst);
                    //if(ShowConstantNodes && IsOnNodeBorder(rayWorldPosition, nodeExtentMin, nodeExtentMax))
                    //    dst += k_ConstantNodeWireColor;
                }
                else
                {
                    if(IsOnNodeBorder(rayWorldPosition, nodeExtentMin, nodeExtentMax))
                        dst += GetNodeWireColor(curDepth);
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

                    /*float interpolation = 1.0f;
                    //curBrickStepSize is the step size for the current brick in world coordinates
                    if(PixelSize > projVoxelSize &&
                       curDepth != 0 && //if we are at root node then can't do interp
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
                    else*/
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
                    //if(IsOnNodeBorder(rayWorldPosition, nodeExtentMin, nodeExtentMax))
                    //    dst += k_NodeWireColor;
                }
                

                if(nodeListTraversalCount >= startPushingNodes
                   && nodeListTraversalCount < stopPushingNodes
                   && pushedNodeCount < MaxNodesToPushThisFrame)
                {
                    ++pushedNodeCount;

                    AddToNodeUsageList(curListIndex, curComponentIndex, nodeUsage);

                }

                if(dst.a > 0.995f && (curNodeIsConstant || brickIsLoaded))
                //only cut this traversal short if the current node's brick or constant value was traversed
                {
                    break;
                    //FragColor = dst;
                    //return;
                }

                //if(traversalCount > MaxTreeTraversals)
                //{
                    //dst = vec4(rayPosition.z, 0, 0, 0.25);
                //    FragColor = vec4(1, 1, 1, 1.0);
                //    return;
                //}
 
                octTreeNode = rootOctTreeNode;
                nodeExtentMin = VolExtentMin;
                nodeExtentMax = VolExtentMax;
                nodeSize = 1.0f;
                voxelHalfSize = RootVoxelHalfSize;

                rayBrickPosition = rayWorldPosition;//position of ray relative to current octree node's brick
                curBrickRayPosition = rayWorldPosition;//position of ray relative to current brick (most recently encounted brick during descent)
                //prevBrickRayPosition = rayWorldPosition;//position of ray relative to previous brick (second most recently encounted brick during descent)
        
                curBrickPtr = octTreeNode;
                //prevBrickPtr = curBrickPtr;
                curConstValue = vec4(0.0f);
                brickIsLoaded = false;
                curNodeIsConstant = RootNodeIsConstant;
                //prevNodeIsConstant = false;
                projVoxelSize = 0.0f;
                //curProjVoxelSize = 0.0f;//TODO, I don't need these anymore
                //prevProjVoxelSize = 0.0f;
                curDepth = 0;
                curBrickDepth = 0;
                childNodePtr = uvec3(0u);
                //distToNearPlane = ComputeDistFromPlane(NearPlane, rayWorldPosition); 
        
                test1 = sign(rayWorldPosition - VolExtentMin);
                test2 = sign(VolExtentMax - rayWorldPosition);
                inside = dot(test1, test2);

                ++traversalCount;
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
