#pragma once

#if defined(_WIN32) || defined(_WIN64) /* { */
#if defined(SPT_BUILD_AS_DLL)          /* { */
#if defined(SPT_EXPORTS)               /* { */
#define SPT_API __declspec(dllexport)
#else /* }{ */
#define SPT_API __declspec(dllimport)
#endif /* } */
#define SPT_API_CLASS SPT_API
#else /* }{ */
#define SPT_API extern
#define SPT_API_CLASS
#endif                        /* } */
#else                         /* }{ */
// 非 Windows 平台（Linux/macOS）
#if defined(SPT_BUILD_AS_DLL) /* { */
#define SPT_API __attribute__((visibility("default")))
#define SPT_API_CLASS __attribute__((visibility("default")))
#else /* }{ */
#define SPT_API extern
#define SPT_API_CLASS
#endif /* } */
#endif /* } */