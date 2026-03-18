#!/bin/bash
# SPT语言测试运行脚本
# 运行test目录下所有.spt文件并报告结果

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_DIR="$PROJECT_DIR/test"
SPTSCRIPT="$PROJECT_DIR/build/bin/sptscript"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# 计数器
passed=0
failed=0
failed_tests=()

echo "SPTScript: $SPTSCRIPT"
echo "Test directory: $TEST_DIR"
echo ""

# 检查sptscript是否存在
if [ ! -f "$SPTSCRIPT" ]; then
    echo "Error: sptscript not found at $SPTSCRIPT"
    exit 1
fi

# 查找所有.spt文件
spt_files=$(find "$TEST_DIR" -name "*.spt" | sort)
total=$(echo "$spt_files" | wc -l)

echo "Found $total test files"
echo "============================================================"

# 运行每个测试文件
while IFS= read -r spt_file; do
    rel_path="${spt_file#$TEST_DIR/}"
    
    # 运行测试
    output=$("$SPTSCRIPT" "$spt_file" 2>&1)
    exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "[${GREEN}PASS${NC}] $rel_path"
        ((passed++))
    else
        echo -e "[${RED}FAIL${NC}] $rel_path"
        echo "       Error: $output"
        failed_tests+=("$rel_path")
        ((failed++))
    fi
done <<< "$spt_files"

echo "============================================================"
echo "Results: $passed/$total passed, $failed failed"

if [ ${#failed_tests[@]} -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for t in "${failed_tests[@]}"; do
        echo "  - $t"
    done
    exit 1
fi

exit 0
