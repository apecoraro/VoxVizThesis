#version 330

uniform sampler3D VoxelSampler;
//uniform sampler1D ColorLUTSampler;

in vec3 TexCoord;
out vec4 FragColor;

void main()
{
    //vec3 xyz = clamp(gl_TexCoord[0].stp, 0.2, 0.8);
    //float voxel = texture3D(VoxelSampler, xyz).r;
    //if(gl_TexCoord[0].s > 1.0 || gl_TexCoord[0].t > 1.0 || gl_TexCoord[0].p > 1.0
    //   || gl_TexCoord[0].s < 0.0 || gl_TexCoord[0].t < 0.0 || gl_TexCoord[0].p < 0.0)
    //{
    //    gl_FragColor = vec4(0.0, 0.0, 0.0, 0.0);
    //    return;
    //}

    //float voxel = texture3D(VoxelSampler, gl_TexCoord[0].stp).r;
    
    vec4 color = texture(VoxelSampler, TexCoord.stp);

    //vec4 color = texture1D(ColorLUTSampler, voxel);

    //gl_FragColor = vec4(0, gl_TexCoord[0].s, 0, 1);
    //gl_FragColor = vec4(0.0, 1.0 - voxel, 0.0, 1.0 - voxel);
    FragColor = color;
}
