#version 330

uniform int NumSamples;
uniform sampler3D VoxelSampler;
//uniform sampler1D ColorLUTSampler;
uniform sampler2D JitterTexSampler;
uniform int JitterTexSize;
uniform float RayStepSize;
uniform vec3 CameraPosition;
uniform vec3 LightPosition;
uniform vec3 VolExtentMin;
uniform vec3 VolExtentMax;
uniform bool ComputeLighting;

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

in vec4 RayPosition;
layout(pixel_center_integer) in ivec4 gl_FragCoord;
out vec4 FragColor;

void main()
{
    float voxel;
    vec4 src;
    
    vec4 dst = vec4(0.0, 0.0, 0.0, 0.0);

    vec3 rayPosition = RayPosition.xyz;

    //jitter start
    vec3 rayDir = normalize(rayPosition - CameraPosition);
    vec3 rayStep = (rayDir * RayStepSize);
    
    ivec2 jitterTexCoord = ivec2(gl_FragCoord.xy % JitterTexSize);
    rayPosition += (rayStep * texelFetch(JitterTexSampler, jitterTexCoord, 0).r);
 
    vec3 nextPosition = rayPosition + rayStep;
    vec3 test1 = sign(nextPosition - VolExtentMin);
    vec3 test2 = sign(VolExtentMax - nextPosition);
    float inside = dot(test1, test2);

    while(inside >= 3.0)
    {
        //voxel = texture(VoxelSampler, rayPosition).r;
        src = texture(VoxelSampler, rayPosition);
        
		if(ComputeLighting)
		{
			//compute gradiant
			vec3 sample1;
			vec3 sample2;

			const ivec4 offset = ivec4(1, 0, 0, -1);
			sample1.x = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.xyz).a;
			sample2.x = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.wyz).a;
			sample1.y = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.yxz).a;
			sample2.y = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.ywz).a;
			sample1.z = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.yzx).a;
			sample2.z = textureOffset(VoxelSampler, 
									  rayPosition,
									  offset.yzw).a;

			vec3 normal = normalize(sample2 - sample1);
			if(isnan(normal) != true)
			{
				vec3 toCamera = normalize(CameraPosition - rayPosition);
				src.rgb += shading(src.rgb, normal, toCamera, normalize(LightPosition - rayPosition));
			}
		}

        //src.rgb *= src.a;
        dst = (1.0 - dst.a) * src + dst;

        if(dst.a > 0.95)
            break;

        rayPosition += rayStep;
        
        test1 = sign(rayPosition - VolExtentMin);
        test2 = sign(VolExtentMax - rayPosition);
        inside = dot(test1, test2);
    }

    FragColor = dst;
}
