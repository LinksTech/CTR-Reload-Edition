@echo off
setlocal

:: CTR Reload Edition - MSVC build (Windows, 32-bit): configure, build, self-tests.
:: Requires Visual Studio 2022 or Build Tools 2022 with the Desktop C++ workload,
:: CMake 3.21 or newer, the Vulkan SDK and Python 3. See BUILDING.md.

where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: cmake not found in PATH
    echo Install CMake 3.21 or newer, or the Visual Studio C++ CMake tools component.
    exit /b 1
)

cmake --preset windows-msvc-x86
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configure failed
    exit /b 1
)

cmake --build --preset windows-msvc-x86-release --parallel
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    exit /b 1
)

ctest --preset windows-msvc-x86-release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Self-tests failed
    exit /b 1
)

echo.
echo Build succeeded: build-msvc-x86\Release\ctr_native.exe
