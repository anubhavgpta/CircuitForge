@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "PATH=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4\bin;%PATH%"
cmake -S . -B build-gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCIRCUITFORGE_CUDA=ON
if errorlevel 1 exit /b 1
cmake --build build-gpu
if errorlevel 1 exit /b 1
ctest --test-dir build-gpu --output-on-failure
