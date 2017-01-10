#version 330

uniform mat4 ProjectionMatrix;
uniform mat4 ModelViewMatrix;

uniform int nSequence[64];
uniform vec3 vecVertices[8];
uniform int v1[24];
uniform int v2[24];

uniform vec3 vecTranslate;
uniform vec3 vecScale;
uniform vec3 vecTexCoordScale;

uniform int frontIdx;
uniform vec3 vecView;

in vec4 glVertex;
out vec3 TexCoord;

void main()
{
    int vtxIndex = int(glVertex.x);
    float planeD = glVertex.y;

    vec3 position = vec3(glVertex.x, glVertex.y, 0.0);

    for(int e = 0; e < 4; ++e)
    {
        int vidx1 = nSequence[int(frontIdx * 8 + v1[vtxIndex * 4 + e])];
        int vidx2 = nSequence[int(frontIdx * 8 + v2[vtxIndex * 4 + e])];

        vec3 rayStart = vecVertices[vidx1] * vecScale;
        rayStart += vecTranslate;

        vec3 rayEnd = vecVertices[vidx2] * vecScale;
        rayEnd += vecTranslate;
        
        vec3 rayDir = rayEnd - rayStart;
        
        float denom = dot(rayDir, vecView);
        
        float lambda = (denom != 0.0) ? 
            ((planeD - dot(rayStart, vecView)) / denom) : -1.0;

        if(lambda >= 0.0 && lambda <= 1.0)
        {
            position = rayStart + lambda * rayDir;
            break;
        }
    }

    gl_Position = ProjectionMatrix * ModelViewMatrix * vec4(position, 1);
    
    vec3 texCoord = position - vecTranslate;
    texCoord /= vecScale;
    //texCoord /= vecTexCoordScale;
    TexCoord = texCoord+0.5;
}
