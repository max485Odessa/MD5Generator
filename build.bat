@echo off
setlocal
cd /d "%~dp0"
set "MD5GEN_CXX=g++.exe"
if exist "C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin\g++.exe" set "MD5GEN_CXX=C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin\g++.exe"
if not "%~1"=="" set "MD5GEN_CXX=%~1"
"%MD5GEN_CXX%" -std=c++11 -O2 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 -mwindows main.cpp core.cpp -o MD5Gen.exe -static-libgcc -static-libstdc++ -ladvapi32 -lcomdlg32 -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built MD5Gen.exe
