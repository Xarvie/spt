#!/usr/bin/env python3
"""
SPT语言测试运行器
运行test目录下所有.spt文件并报告结果
"""

import os
import sys
import subprocess
import glob


def run_spt_tests(test_dir, sptscript_exe):
    """运行所有.spt测试文件"""
    failed_tests = []
    passed_count = 0

    # 查找所有.spt文件
    spt_files = glob.glob(os.path.join(test_dir, "**", "*.spt"), recursive=True)
    spt_files.sort()

    total = len(spt_files)
    print(f"Found {total} test files")
    print("=" * 60)

    for spt_file in spt_files:
        rel_path = os.path.relpath(spt_file, test_dir)
        try:
            result = subprocess.run(
                [sptscript_exe, spt_file],
                capture_output=True,
                text=True,
                timeout=30
            )
            if result.returncode == 0:
                print(f"[PASS] {rel_path}")
                passed_count += 1
            else:
                print(f"[FAIL] {rel_path}")
                print(f"       Error: {result.stderr.strip()}")
                failed_tests.append(rel_path)
        except subprocess.TimeoutExpired:
            print(f"[TIMEOUT] {rel_path}")
            failed_tests.append(rel_path)
        except Exception as e:
            print(f"[ERROR] {rel_path}: {e}")
            failed_tests.append(rel_path)

    print("=" * 60)
    print(f"Results: {passed_count}/{total} passed, {len(failed_tests)} failed")

    if failed_tests:
        print("\nFailed tests:")
        for t in failed_tests:
            print(f"  - {t}")
        return 1
    return 0


def main():
    # 获取脚本所在目录
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)

    test_dir = os.path.join(project_dir, "test")
    sptscript_exe = os.path.join(project_dir, "build", "bin", "sptscript")

    # Windows上添加.exe后缀
    if sys.platform == "win32":
        sptscript_exe += ".exe"

    if not os.path.exists(sptscript_exe):
        print(f"Error: sptscript not found at {sptscript_exe}")
        return 1

    if not os.path.exists(test_dir):
        print(f"Error: test directory not found at {test_dir}")
        return 1

    print(f"SPTScript: {sptscript_exe}")
    print(f"Test directory: {test_dir}")
    print()

    return run_spt_tests(test_dir, sptscript_exe)


if __name__ == "__main__":
    sys.exit(main())
