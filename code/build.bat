@echo off

set CodeDir=..\code
set DataDir=..\data
set LibsDir=..\libs
set OutputDir=..\build_win32
set VulkanIncludeDir="C:\VulkanSDK\1.2.135.0\Include\vulkan"
set VulkanBinDir="C:\VulkanSDK\1.2.135.0\Bin"
set AssimpDir=%LibsDir%\framework_vulkan

set CommonCompilerFlags=-Od -MTd -nologo -fp:fast -fp:except- -EHsc -Gm- -GR- -EHa- -Zo -Oi -WX -W4 -wd4310 -wd4127 -wd4201 -wd4100 -wd4189 -wd4505 -Z7 -FC
set CommonCompilerFlags=-I %VulkanIncludeDir% %CommonCompilerFlags%
set CommonCompilerFlags=-I %LibsDir% -I %AssimpDir% %CommonCompilerFlags%
REM Check the DLLs here
set CommonLinkerFlags=-incremental:no -opt:ref user32.lib gdi32.lib Winmm.lib opengl32.lib DbgHelp.lib d3d12.lib dxgi.lib d3dcompiler.lib %AssimpDir%\assimp\libs\assimp-vc142-mt.lib

IF NOT EXIST %OutputDir% mkdir %OutputDir%

pushd %OutputDir%

del *.pdb > NUL 2> NUL

REM USING GLSL IN VK USING GLSLANGVALIDATOR
call glslangValidator -DGRID_FRUSTUM=1 -S comp -e main -g -V -o %DataDir%\tiled_deferred_grid_frustum.spv %CodeDir%\tiled_deferred_shaders.cpp
call glslangValidator -DLIGHT_CULLING=1 -S comp -e main -g -V -o %DataDir%\tiled_deferred_light_culling.spv %CodeDir%\tiled_deferred_shaders.cpp
call glslangValidator -DGBUFFER_VERT=1 -S vert -e main -g -V -o %DataDir%\tiled_deferred_gbuffer_vert.spv %CodeDir%\tiled_deferred_shaders.cpp
call glslangValidator -DGBUFFER_FRAG=1 -S frag -e main -g -V -o %DataDir%\tiled_deferred_gbuffer_frag.spv %CodeDir%\tiled_deferred_shaders.cpp
call glslangValidator -DTILED_DEFERRED_LIGHTING_VERT=1 -S vert -e main -g -V -o %DataDir%\tiled_deferred_lighting_vert.spv %CodeDir%\tiled_deferred_shaders.cpp
call glslangValidator -DTILED_DEFERRED_LIGHTING_FRAG=1 -S frag -e main -g -V -o %DataDir%\tiled_deferred_lighting_frag.spv %CodeDir%\tiled_deferred_shaders.cpp

REM Shadows
call glslangValidator -DSHADOW_VERT=1 -S vert -e main -g -V -o %DataDir%\shadow_vert.spv %CodeDir%\tiled_deferred_shaders.cpp

REM Height Map
call glslangValidator -DGBUFFER_VERT=1 -S vert -e main -g -V -o %DataDir%\height_map_gbuffer_vert.spv %CodeDir%\height_map_shaders.cpp
call glslangValidator -DGBUFFER_FRAG=1 -S frag -e main -g -V -o %DataDir%\height_map_gbuffer_frag.spv %CodeDir%\height_map_shaders.cpp
call glslangValidator -DSHADOW_VERT=1 -S vert -e main -g -V -o %DataDir%\height_map_shadow_vert.spv %CodeDir%\height_map_shaders.cpp

REM USING HLSL IN VK USING DXC
REM set DxcDir=C:\Tools\DirectXShaderCompiler\build\Debug\bin
REM %DxcDir%\dxc.exe -spirv -T cs_6_0 -E main -fspv-target-env=vulkan1.1 -Fo ..\data\write_cs.o -Fh ..\data\write_cs.o.txt ..\code\bw_write_shader.cpp

REM ASSIMP
copy %AssimpDir%\assimp\bin\assimp-vc142-mt.dll %OutputDir%\assimp-vc142-mt.dll

REM 64-bit build
echo WAITING FOR PDB > lock.tmp
cl %CommonCompilerFlags% %CodeDir%\height_map_demo.cpp -Fmheight_map_demo.map -LD /link %CommonLinkerFlags% -incremental:no -opt:ref -PDB:height_map_demo_%random%.pdb -EXPORT:Init -EXPORT:Destroy -EXPORT:SwapChainChange -EXPORT:CodeReload -EXPORT:MainLoop
del lock.tmp
call cl %CommonCompilerFlags% -DDLL_NAME=height_map_demo -Feheight_map_demo.exe %LibsDir%\framework_vulkan\win32_main.cpp -Fmheight_map_demo.map /link %CommonLinkerFlags%

popd
