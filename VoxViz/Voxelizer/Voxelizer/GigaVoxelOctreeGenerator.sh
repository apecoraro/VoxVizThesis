#!/bin/bash
#export PATH="/cygdrive/c/Users/apecoraro/Documents/Visual Studio 2010/Projects/OpenSceneGraph-3.0.1/build_vs2010/bin:$PATH"
export PATH="/cygdrive/c/Users/apecoraro/Documents/Visual Studio 2010/Projects/OpenSceneGraph-3.0.1-VS10.0.30319-x64-release-12741/bin:$PATH"

voxelizer="../x64/Release/Voxelizer.exe"
input="../../../geotypic_ive_dds/master.osg --geocentric"
#voxelsSizeArg="--max-voxels-size 960 960 960"
voxelsSizeArg="--max-voxels-size 1920 1920 1920"
outputCountArg="--output-count 4"
#filterArg="--filter-bbox 2325.0 -5842.0 3957.0 -4210.0"
#filterArg="--filter-bbox 2325.0 -5842.0 2901.0  -5266.0"
filterArg="--filter-bbox 2325.0 -5842.0 2517.0  -5650.0"
terrainTextures="--terrain-texture-prefix 0_ --terrain-texture asphalt_path.tga --terrain-texture grass_sand2.tga --terrain-texture runway.tga --terrain-texture taxiway.tga --terrain-texture train1.tga --terrain-texture water.tga"
outputDir="Demo"

echo "Starting gridded voxelization with $voxelizer."

failures=0
while [ -e "$voxelizer" ]
do
    startGridArg=""
    if [ -e $outputDir/pgvot_progress.txt ]
    then
        gridX=`cat $outputDir/pgvot_progress.txt | awk '{ print $1; }'`
        gridY=`cat $outputDir/pgvot_progress.txt | awk '{ print $2; }'`
        gridZ=`cat $outputDir/pgvot_progress.txt | awk '{ print $3; }'`
        startGridArg="--grid-start-x $gridX --grid-start-y $gridY --grid-start-z $gridZ"
        echo "Voxelizer failed, restarting at "`cat $outputDir/pgvot_progress.txt`
        failures=1
    else
        echo "Voxelizer starting. Logging to voxelizer.log"
        if [ -e voxelizer.log ]
        then
            rm voxelizer.log
        fi
    fi

    echo "$voxelizer $voxelsSizeArg $outputCountArg --input $input $startGridArg $filterArg $terrainTextures --output $outputDir >> voxelizer.log 2>&1"
    $voxelizer $voxelsSizeArg $outputCountArg --input $input $startGridArg $filterArg $terrainTextures --output $outputDir 2>&1 | tee -a voxelizer.log 2>&1
    if [ ! -e $outputDir/pgvot_progress.txt ]
    then
        echo "Voxelizer finished."
        break
    fi
done

if [ $failures -eq 1 ]
then
    echo "Generating Root File."
    echo "$voxelizer $voxelsSizeArg --input $input --generate-root-file-only $filterArg --output $outputDir >> voxelizer.log 2>&1"
    $voxelizer $voxelsSizeArg --input $input --generate-root-file-only $filterArg --output $outputDir >> voxelizer.log 2>&1
fi
