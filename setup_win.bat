@echo off
setlocal EnableDelayedExpansion

echo ==================================================
echo   ShortVideo SDK - Windows CMake Setup and Build
echo ==================================================
echo.

:: Check if CMake is available in PATH
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake is not found in your PATH.
    echo Please install CMake from https://cmake.org/download/ and ensure it is added to your system PATH.
    pause
    goto :EOF
)

:: Create build directory
if not exist "build" (
    echo [INFO] Creating build directory...
    mkdir build
)

cd build

:: Configure the project using CMake
echo [INFO] Configuring the project with CMake...
cmake ..
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed.
    pause
    goto :EOF
)

:: Build the project
echo.
echo [INFO] Building the project...
cmake --build . --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed.
    pause
    goto :EOF
)

:: Run the tests
echo.
echo ==================================================
echo   Running C++ Filter Graph Tests...
echo ==================================================
if exist "Release\test_filter_graph.exe" (
    .\Release\test_filter_graph.exe
) else if exist "test_filter_graph.exe" (
    .\test_filter_graph.exe
) else (
    echo [ERROR] Test executable not found!
    pause
    goto :EOF
)

echo.
echo [INFO] Build and test completed successfully!
pause
