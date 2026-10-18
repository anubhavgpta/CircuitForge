@echo off
set "PATH=C:\Qt\6.12.0\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"
cmake -S gui/CircuitForgeDesktop -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
if errorlevel 1 exit /b 1
cmake --build build-desktop
if errorlevel 1 exit /b 1
ctest --test-dir build-desktop --output-on-failure
