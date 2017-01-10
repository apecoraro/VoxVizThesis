#version 330

uniform usampler2D SelectionMaskHistoPyramidSampler;
uniform int SelectionMaskHistoPyramidLevelCount;
uniform int CompressedSelectionMaskWidth;
uniform uint CompressedSelectionMaskMaxIndex;
uniform int MaxNodesToPushThisFrame;

const ivec2 k_SameLevelTraversalIncr[9] = ivec2[](
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
);

uniform usampler2DArray NodeUsageListSampler;
uniform usampler2D SelectionMaskSampler;

const int k_NumNodeLists = 3;
const int k_NumComponentsPerList = 4;
const int k_NumSelectionMaskBits = 12; //k_NumNodeLists * k_NumComponentsPerList;
const ivec2 k_NodeListIndices[12] = ivec2[12](
    ivec2(0, 0), ivec2(0, 1), ivec2(0, 2), ivec2(0, 3),
    ivec2(1, 0), ivec2(1, 1), ivec2(1, 2), ivec2(1, 3),
    ivec2(2, 0), ivec2(2, 1), ivec2(2, 2), ivec2(2, 3)
);
const int k_OutputIndices[12] = int[12](
    0, 1, 2, 3, 
    0, 1, 2, 3,
    0, 1, 2, 3
);

layout(pixel_center_integer) in ivec4 gl_FragCoord;

out uvec4 UniqueNodeUsage;
//use the selection mask to pair down the usage lists to only those that are unique to the current pixel/ray 
void main()
{
    uint key = uint((gl_FragCoord.y * CompressedSelectionMaskWidth) + gl_FragCoord.x);

    if(key > CompressedSelectionMaskMaxIndex)
    {
        //UniqueNodeUsage = uvec4(0u, 0u, 0u, 0u);
        //return;
        discard;
    }

    //traverse the histopyramid to find the coordinates
    //of the selection mask texel to use
    uint start = 0u;
    ivec2 texCoord = ivec2(0, 0);
    ivec2 texSize = ivec2(1, 1);
    //start at lowest detail mip map level    
    int pyramidLevel = SelectionMaskHistoPyramidLevelCount - 1;
    int curLevelLoopCount = 0;
    
    uint end = 0u;
    while(pyramidLevel >= 0)
    {
        end = start + texelFetch(SelectionMaskHistoPyramidSampler, texCoord, pyramidLevel).r;

        if(key >= start && key < end)
        {
            --pyramidLevel;//go to next level
            curLevelLoopCount = 0;
            texCoord <<= 1;
            if(pyramidLevel >= 0)
                texSize = textureSize(SelectionMaskHistoPyramidSampler, pyramidLevel);
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
                UniqueNodeUsage = uvec4(start, end, key, pyramidLevel);
                return;
            }
        }
    }

    //divide by two because we did one extra shift in order
    //to get pyramidLevel == -1
    texCoord >>= 1;
    //use the final tex coord computed in the loop above to
    //lookup the selection mask bits
    uvec4 selectionMaskTexel = texelFetch(SelectionMaskSampler, texCoord, 0);

    uint selectionMaskBits = selectionMaskTexel.r;
    ivec3 nodeUsageListTexCoord = ivec3(selectionMaskTexel.gb, 0);

    //offsets for lists 2 and 3 
    const ivec3 list1 = ivec3(0, 0, 1);
    const ivec3 list2 = ivec3(0, 0, 2);
    uvec4 nodeUsageList[3] = uvec4[](
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord, 0),
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord + list1, 0),
        texelFetch(NodeUsageListSampler, nodeUsageListTexCoord + list2, 0)
    );

    uvec4 nodeListItems = uvec4(0u, 0u, 0u, 0u);
    int curOutputCompIndex = 0;
    int pushedNodes = 0;
    //for each set bit in the selection mask grab the corresponding
    //item from the node usage list and store in the unique node usage output
    for(int itemIndex = 0; 
        itemIndex < k_NumSelectionMaskBits 
        && selectionMaskBits != 0u
        && pushedNodes < MaxNodesToPushThisFrame;//k_NumComponentsPerList;//as soon as we have stored 4 then quit
        ++itemIndex, selectionMaskBits >>= 1u)
    {
        uint testBit = selectionMaskBits & 1u;
        if(testBit != 0u)
        {
            ivec2 indices = k_NodeListIndices[itemIndex];
            uint nodeListItem = nodeUsageList[indices.x][indices.y];

            nodeListItems[k_OutputIndices[pushedNodes]] = nodeListItem;

            ++pushedNodes;
        }
    }

    UniqueNodeUsage = nodeListItems;
}
