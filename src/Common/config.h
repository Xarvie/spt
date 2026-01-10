#pragma once

#if defined(SPT_BUILD_AS_DLL) /* { */
#if defined(SPT_EXPORTS) /* { */
#define SPT_API __declspec(dllexport)
#else /* }{ */
#define SPT_API __declspec(dllimport)
#endif /* } */
#define SPT_API_CLASS SPT_API
#else /* }{ */

#define SPT_API extern
#define SPT_API_CLASS

#endif /* } */