@echo off
setlocal
cd /d "%~dp0"
set "MD5GEN_CXX=g++.exe"
if exist "C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin\g++.exe" set "MD5GEN_CXX=C:\Program Files (x86)\Embarcadero\Dev-Cpp\TDM-GCC-64\bin\g++.exe"
if not "%~1"=="" set "MD5GEN_CXX=%~1"
"%MD5GEN_CXX%" -std=c++11 -O2 -Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 tests\core_tests.cpp core.cpp -o core_tests.exe -static-libgcc -static-libstdc++ -ladvapi32
if errorlevel 1 exit /b 1
core_tests.exe
exit /b %errorlevel%
