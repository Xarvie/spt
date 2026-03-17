@echo off
set SPT_BIN=C:\Users\ftp\Desktop\spt\build\bin\sptscript.exe
set TEST_DIR=C:\Users\ftp\Desktop\spt\spt-lsp\test

echo Running import/export tests...
echo.

echo Test 1: Multiple imports
call "%SPT_BIN%" "%TEST_DIR%\test_import_multiple.spt"
if errorlevel 1 echo FAILED: test_import_multiple.spt & exit /b 1
echo PASSED: test_import_multiple.spt
echo.

echo Test 2: Nested imports
call "%SPT_BIN%" "%TEST_DIR%\test_import_nested.spt"
if errorlevel 1 echo FAILED: test_import_nested.spt & exit /b 1
echo PASSED: test_import_nested.spt
echo.

echo Test 3: Math module
call "%SPT_BIN%" "%TEST_DIR%\test_import_math.spt"
if errorlevel 1 echo FAILED: test_import_math.spt & exit /b 1
echo PASSED: test_import_math.spt
echo.

echo Test 4: String utils
call "%SPT_BIN%" "%TEST_DIR%\test_import_string_utils.spt"
if errorlevel 1 echo FAILED: test_import_string_utils.spt & exit /b 1
echo PASSED: test_import_string_utils.spt
echo.

echo Test 5: List utils
call "%SPT_BIN%" "%TEST_DIR%\test_import_list_utils.spt"
if errorlevel 1 echo FAILED: test_import_list_utils.spt & exit /b 1
echo PASSED: test_import_list_utils.spt
echo.

echo Test 6: Combined imports
call "%SPT_BIN%" "%TEST_DIR%\test_import_combined.spt"
if errorlevel 1 echo FAILED: test_import_combined.spt & exit /b 1
echo PASSED: test_import_combined.spt
echo.

echo Test 7: Empty module
call "%SPT_BIN%" "%TEST_DIR%\test_import_empty.spt"
if errorlevel 1 echo FAILED: test_import_empty.spt & exit /b 1
echo PASSED: test_import_empty.spt
echo.

echo Test 8: Functions only
call "%SPT_BIN%" "%TEST_DIR%\test_import_only_functions.spt"
if errorlevel 1 echo FAILED: test_import_only_functions.spt & exit /b 1
echo PASSED: test_import_only_functions.spt
echo.

echo All tests passed!
