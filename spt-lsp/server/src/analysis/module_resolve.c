/*
** module_resolve.c — SPT 模块路径解析实现。
**
** 搜索顺序（根 README §14.3）：
**   script_dir/<module>.spt  ->  $SPT_PATH 各分号段/<module>.spt  ->  ./<module>.spt
** 跨平台：Windows 用 '\' 分隔并靠 GetFileAttributesA 判存在；POSIX 用 '/' + stat。
*/
#include "module_resolve.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

/* 文件是否存在（常规文件）。 */
static int file_exists(const char *path) {
#ifdef _WIN32
  DWORD a = GetFileAttributesA(path);
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

/* 取路径的目录部分（含末尾分隔符），写入 out。无分隔符则 out="." + 分隔符。 */
static void dir_of(const char *path, char *out, size_t cap) {
  const char *slash = NULL;
  for (const char *p = path; *p; p++) {
#ifdef _WIN32
    if (*p == '\\' || *p == '/') slash = p;
#else
    if (*p == '/') slash = p;
#endif
  }
  if (slash) {
    size_t n = (size_t)(slash - path);
    if (n + 2 >= cap) n = cap - 2;
    memcpy(out, path, n);
    out[n] = '\0';
  } else {
    snprintf(out, cap, ".");
  }
}

/* 拼接 dir + sep + name + ".spt" 到 out。 */
static void join_spt(const char *dir, const char *name, char *out, size_t cap) {
#ifdef _WIN32
  char sep = (dir[0] && (dir[strlen(dir) - 1] == '\\' || dir[strlen(dir) - 1] == '/')) ? '\0' : '\\';
  if (sep) snprintf(out, cap, "%s\\%s.spt", dir, name);
  else snprintf(out, cap, "%s%s.spt", dir, name);
#else
  char sep = (dir[0] && dir[strlen(dir) - 1] == '/') ? '\0' : '/';
  if (sep) snprintf(out, cap, "%s/%s.spt", dir, name);
  else snprintf(out, cap, "%s%s.spt", dir, name);
#endif
}

int resolve_module_path(const char *from_path, const char *module_name, char *out, size_t cap) {
  if (!from_path || !module_name || !out || cap == 0) return 0;
  /* module_name 不允许含路径分隔符或点分（README 明确不支持）。 */
  for (const char *p = module_name; *p; p++) {
#ifdef _WIN32
    if (*p == '/' || *p == '\\') return 0;
#else
    if (*p == '/') return 0;
#endif
  }

  /* 1. script_dir/<module>.spt */
  char dir[4096];
  dir_of(from_path, dir, sizeof dir);
  join_spt(dir, module_name, out, cap);
  if (file_exists(out)) return 1;

  /* 2. $SPT_PATH 各分号段/<module>.spt */
  const char *spt_path = getenv("SPT_PATH");
  if (spt_path && spt_path[0]) {
    const char *seg = spt_path;
    while (*seg) {
      const char *semi = strchr(seg, ';');
      size_t len = semi ? (size_t)(semi - seg) : strlen(seg);
      if (len > 0 && len < sizeof dir) {
        memcpy(dir, seg, len);
        dir[len] = '\0';
        join_spt(dir, module_name, out, cap);
        if (file_exists(out)) return 1;
      }
      if (!semi) break;
      seg = semi + 1;
    }
  }

  /* 3. ./<module>.spt */
  join_spt(".", module_name, out, cap);
  if (file_exists(out)) return 1;

  out[0] = '\0';
  return 0;
}
