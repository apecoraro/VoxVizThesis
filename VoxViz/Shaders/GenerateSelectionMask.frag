#version 330

uniform usampler2DArray NodeUsageListSampler;
uniform vec2 NodeUsageListDimMinusTwo;

const int k_NumNodeLists = 3;
const int k_NumComponentsPerList = 4;
const int k_NumNodeListItems = 12;//k_NumNodeLists * k_NumComponentsPerList;

layout(pixel_center_integer) in ivec4 gl_FragCoord;

//in vec2 TexCoord;
out uvec4 Output;

void main()
{
    //convert tex coord to 2D array integer indices
    //ivec2 texel = ivec2(TexCoord * NodeUsageListDimMinusTwo);
    ivec3 texel = ivec3(gl_FragCoord.xy, 0);
 
    //save the pixel/texel position, store 0 at red for now
    Output = uvec4(0u, texel.x, texel.y, 1u);

    //offsets for lists 2 and 3 
    const ivec3 list1 = ivec3(0, 0, 1);
    const ivec3 list2 = ivec3(0, 0, 2);

    uvec4 nodeUsageList[k_NumNodeLists] = uvec4[k_NumNodeLists](
        texelFetch(NodeUsageListSampler, texel, 0),
        texelFetch(NodeUsageListSampler, texel + list1, 0),
        texelFetch(NodeUsageListSampler, texel + list2, 0)
    );

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

        topNodeUsageList = uvec4[k_NumNodeLists](
            texelFetch(NodeUsageListSampler, neighbor, 0),
            texelFetch(NodeUsageListSampler, neighbor + list1, 0),
            texelFetch(NodeUsageListSampler, neighbor + list2, 0)
        );

        if(texel.x < NodeUsageListDimMinusTwo.x)
        {
            //sample to right
            neighbor.x += 2;

            topRightNodeUsageList = uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            );
    
            neighbor.y = texel.y;

            rightNodeUsageList = uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            );
        }
        else
        {
            topRightNodeUsageList = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
            rightNodeUsageList = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        }
    }
    else
    {
        topNodeUsageList = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        topRightNodeUsageList = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
        if(texel.x < NodeUsageListDimMinusTwo.x)
        {
            ivec3 neighbor = ivec3(texel.x + 2, texel.y, 0);

            rightNodeUsageList = uvec4[k_NumNodeLists](
                texelFetch(NodeUsageListSampler, neighbor, 0),
                texelFetch(NodeUsageListSampler, neighbor + list1, 0),
                texelFetch(NodeUsageListSampler, neighbor + list2, 0)
            );
        }
        else
            rightNodeUsageList = uvec4[k_NumNodeLists]( uvec4(0u), uvec4(0u), uvec4(0u) );
    }

    int prevListIndex;
    int curListIndex = k_NumNodeLists-1;
    int nextListIndex = 0;

    int prevCompIndex;
    int curCompIndex = k_NumComponentsPerList-1;
    int nextCompIndex = 0;

    uint nodeUsage;
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
            if(nextListIndex == k_NumNodeLists)//TODO can get rid of this if add extra empty list to neighbor lists
            {
                nextListIndex = 0;//need this to be a valid index
            }
        }

        nodeUsage = nodeUsageList[curListIndex][curCompIndex];
            
        if(nodeUsage == 0u)
            return;//if zero then we've hit the end of this list, so we can quit

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
        Output.r |= (1u << uint(nodeListIndex));
    }
}
