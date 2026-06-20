#!/usr/bin/env python3
"""代码行数统计工具

用法:
    python count_lines.py [目录或文件...] [选项]

选项:
    -e, --ext      指定扩展名 (逗号分隔, 如 .c,.h,.py)
    -v, --verbose  显示每个文件的详细统计
    --no-comment   不统计注释行
"""

import os
import sys
import argparse

# 常见语言的注释规则
COMMENT_RULES = {
    '.c': ('//', ('/*', '*/')),
    '.h': ('//', ('/*', '*/')),
    '.cpp': ('//', ('/*', '*/')),
    '.hpp': ('//', ('/*', '*/')),
    '.cc': ('//', ('/*', '*/')),
    '.cxx': ('//', ('/*', '*/')),
    '.py': ('#', None),
    '.sh': ('#', None),
    '.bash': ('#', None),
    '.js': ('//', ('/*', '*/')),
    '.ts': ('//', ('/*', '*/')),
    '.tsx': ('//', ('/*', '*/')),
    '.jsx': ('//', ('/*', '*/')),
    '.java': ('//', ('/*', '*/')),
    '.go': ('//', ('/*', '*/')),
    '.rs': ('//', ('/*', '*/')),
    '.swift': ('//', ('/*', '*/')),
    '.kt': ('//', ('/*', '*/')),
    '.lua': ('--', ('--[[', ']]')),
    '.spt': ('//', ('/*', '*/')),
    '.rb': ('#', None),
    '.php': ('//', ('/*', '*/')),
    '.css': (None, ('/*', '*/')),
    '.html': (None, ('<!--', '-->')),
    '.xml': (None, ('<!--', '-->')),
    '.sql': ('--', ('/*', '*/')),
}

DEFAULT_EXTS = {'.c', '.h', '.cpp', '.hpp', '.cc', '.cxx', '.py', '.sh',
                '.js', '.ts', '.java', '.go', '.rs', '.spt', '.lua'}


def count_file(filepath, line_comment, block_comment, count_comments=True):
    """统计单个文件的代码行、空行、注释行"""
    code_lines = 0
    blank_lines = 0
    comment_lines = 0
    in_block = False

    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            for raw in f:
                line = raw.strip()
                if not line:
                    blank_lines += 1
                    continue

                if in_block:
                    # 在块注释中
                    if block_comment and block_comment[1] in line:
                        in_block = False
                    if count_comments:
                        comment_lines += 1
                    continue

                # 检查块注释开始
                if block_comment and block_comment[0] in line:
                    # 整行是块注释开始
                    idx = line.find(block_comment[0])
                    rest = line[:idx].strip()
                    if not rest:
                        # 纯注释行
                        if block_comment[1] in line and line.find(block_comment[1]) > idx:
                            pass  # 单行块注释 /* ... */
                        else:
                            in_block = True
                        if count_comments:
                            comment_lines += 1
                        continue
                    else:
                        # 代码 + 注释混合行，算代码
                        code_lines += 1
                        if block_comment[1] not in line:
                            in_block = True
                        continue

                # 检查行注释
                if line_comment and line.startswith(line_comment):
                    if count_comments:
                        comment_lines += 1
                    continue

                # 普通代码行
                code_lines += 1
    except (IOError, OSError) as e:
        print(f"  警告: 无法读取 {filepath}: {e}", file=sys.stderr)

    return code_lines, blank_lines, comment_lines


def walk_paths(paths, exts):
    """遍历路径，返回匹配扩展名的文件列表"""
    files = []
    for p in paths:
        if os.path.isfile(p):
            ext = os.path.splitext(p)[1].lower()
            if not exts or ext in exts:
                files.append(p)
        elif os.path.isdir(p):
            for root, dirs, fnames in os.walk(p):
                # 跳过常见忽略目录
                dirs[:] = [d for d in dirs if d not in
                           {'.git', '.svn', 'build', 'build_mingw', 'node_modules',
                            '__pycache__', '.vscode', 'CMakeFiles', '.cache'}]
                for fn in sorted(fnames):
                    ext = os.path.splitext(fn)[1].lower()
                    if not exts or ext in exts:
                        files.append(os.path.join(root, fn))
    return files


def main():
    parser = argparse.ArgumentParser(description='代码行数统计工具')
    parser.add_argument('paths', nargs='*', default=['.'],
                        help='要统计的目录或文件 (默认当前目录)')
    parser.add_argument('-e', '--ext', default=None,
                        help='指定扩展名 (逗号分隔, 如 .c,.h,.py)')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='显示每个文件的详细统计')
    parser.add_argument('--no-comment', action='store_true',
                        help='不统计注释行')
    args = parser.parse_args()

    # 解析扩展名
    if args.ext:
        exts = {e if e.startswith('.') else '.' + e for e in args.ext.split(',')}
    else:
        exts = DEFAULT_EXTS

    files = walk_paths(args.paths, exts)
    if not files:
        print("未找到匹配的文件。")
        print(f"搜索路径: {args.paths}")
        print(f"扩展名: {sorted(exts)}")
        return 1

    # 按扩展名分组统计
    total_code = 0
    total_blank = 0
    total_comment = 0
    by_ext = {}

    for fp in files:
        ext = os.path.splitext(fp)[1].lower()
        rules = COMMENT_RULES.get(ext, (None, None))
        line_c = rules[0]
        block_c = rules[1] if rules[1] else None

        code, blank, comment = count_file(
            fp, line_c, block_c, count_comments=not args.no_comment)

        total_code += code
        total_blank += blank
        total_comment += comment

        if ext not in by_ext:
            by_ext[ext] = [0, 0, 0, 0]
        by_ext[ext][0] += code
        by_ext[ext][1] += blank
        by_ext[ext][2] += comment
        by_ext[ext][3] += 1

        if args.verbose:
            total = code + blank + comment
            print(f"  {fp:<60} {total:>7} (code={code} blank={blank} comment={comment})")

    # 打印汇总
    total_all = total_code + total_blank + total_comment
    print("=" * 70)
    print(f"  文件数: {len(files)}")
    print(f"  总行数: {total_all:>8}")
    print(f"  代码行: {total_code:>8}  ({total_code/total_all*100:.1f}%)")
    print(f"  空行:   {total_blank:>8}  ({total_blank/total_all*100:.1f}%)")
    if not args.no_comment:
        print(f"  注释行: {total_comment:>8}  ({total_comment/total_all*100:.1f}%)")
    print("=" * 70)

    # 按扩展名分组
    print(f"\n  按扩展名:")
    print(f"  {'扩展名':<8} {'文件数':>6} {'代码行':>8} {'空行':>8} {'注释行':>8} {'合计':>8}")
    print(f"  {'-'*8} {'-'*6} {'-'*8} {'-'*8} {'-'*8} {'-'*8}")
    for ext in sorted(by_ext.keys()):
        c, b, cm, n = by_ext[ext]
        t = c + b + cm
        print(f"  {ext:<8} {n:>6} {c:>8} {b:>8} {cm:>8} {t:>8}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
