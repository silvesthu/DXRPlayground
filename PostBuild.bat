@echo off
setlocal

set "ProjectDir=%~1"
set "TargetDir=%~2"

REM Prepare Shader
mkdir "%ProjectDir%..\Shader\nvapi"
copy "%ProjectDir%..\Source\Thirdparty\nvapi\nvHLSLExtns.h" "%ProjectDir%..\Shader\nvapi"
copy "%ProjectDir%..\Source\Thirdparty\nvapi\nvHLSLExtnsInternal.h" "%ProjectDir%..\Shader\nvapi"
copy "%ProjectDir%..\Source\Thirdparty\nvapi\nvShaderExtnEnums.h" "%ProjectDir%..\Shader\nvapi"
mkdir "%ProjectDir%..\Shader\ShaderToHuman"
copy "%ProjectDir%..\Source\Thirdparty\ShaderToHuman\docs\include\*.hlsl" "%ProjectDir%..\Shader\ShaderToHuman"
mkdir "%ProjectDir%..\Shader\nanovdb"
copy "%ProjectDir%..\Source\Thirdparty\openvdb\nanovdb\PNanoVDB.h" "%ProjectDir%..\Shader\nanovdb"

REM Prepare Asset
if not exist "%ProjectDir%..\Asset\ArPragueSkyModelGround\SkyModelDataset.dat" powershell Expand-Archive "%ProjectDir%..\Asset\ArPragueSkyModelGround\SkyModelDataset.zip" -DestinationPath "%ProjectDir%..\Asset\ArPragueSkyModelGround"

REM Link Asset
if not exist "%TargetDir%Shader" mklink /D "%TargetDir%Shader" "%ProjectDir%..\Shader"
if not exist "%TargetDir%Asset" mklink /D "%TargetDir%Asset" "%ProjectDir%..\Asset"

exit /B 0
