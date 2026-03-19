@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
cd C:\Users\ftp\Desktop\spt\spt-lsp\build
cmake ..
cmake --build .
.\tests\Debug\lsp_tests2.exe
