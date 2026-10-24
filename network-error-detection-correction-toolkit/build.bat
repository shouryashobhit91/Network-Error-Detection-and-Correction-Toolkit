@echo off
REM build.bat - builds all programs on Windows with MinGW-w64 g++ (C++17).
if not exist bin mkdir bin
if not exist results mkdir results
if not exist graphs mkdir graphs
set CODES=src\codes.cpp src\errors.cpp
set NET=src\codes.cpp src\errors.cpp src\sha256.cpp src\link.cpp
set FLAGS=-std=c++17 -O2 -Wall -Isrc -static -pthread
set LIBS=-lws2_32
echo Building edc_toolkit ...
g++ %FLAGS% %CODES% src\toolkit_main.cpp -o bin\edc_toolkit.exe || goto err
echo Building edc_sender ...
g++ %FLAGS% %NET% src\sender_main.cpp -o bin\edc_sender.exe %LIBS% || goto err
echo Building edc_receiver ...
g++ %FLAGS% %NET% src\receiver_main.cpp -o bin\edc_receiver.exe %LIBS% || goto err
echo Building edc_channel ...
g++ %FLAGS% %NET% src\channel_main.cpp -o bin\edc_channel.exe %LIBS% || goto err
echo Building edc_tests ...
g++ %FLAGS% %NET% tests\test_main.cpp -o bin\edc_tests.exe %LIBS% || goto err
echo Building edc_bench ...
g++ %FLAGS% %NET% src\bench_main.cpp -o bin\edc_bench.exe %LIBS% || goto err
echo.
echo Done. Programs are in the bin folder.
goto end
:err
echo BUILD FAILED
exit /b 1
:end
