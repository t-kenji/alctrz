/** @file       debug.h
 *  @brief      デバッグに関連する機能を提供する.
 *
 *  @author     t-kenji <protect.2501@gmail.com>
 *  @date       2018-04-30 新規作成.
 *  @copyright  Copyright © 2018 t-kenji
 *
 *  This code is licensed under the MIT License.
 */
#ifndef __ALCATRAZ_DEBUG_H__
#define __ALCATRAZ_DEBUG_H__

#if !defined(DEBUG_FILE)
#define DEBUG_FILE stdout
#endif

#if !defined(ERROR_FILE)
#define ERROR_FILE stderr
#endif

#undef STATIC
#undef INLINE
#if INTERNAL_TESTABLE == 1
#define STATIC
#define INLINE
#else
#define STATIC static
#define INLINE static inline
#endif

/**
 *  デバッグ出力マクロ.
 *
 *  @param  [in]    format  出力文字列書式.
 *  @param  [in]    ...     書式パラメータ.
 */
#if NODEBUG == 0
#define DEBUG(format, ...)                                    \
    do {                                                      \
        fprintf(DEBUG_FILE, "%s:%d(%s) " format "\r\n",       \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG(format, ...)
#endif

/**
 *  エラー出力マクロ.
 *
 *  @param  [in]    format  出力文字列書式.
 *  @param  [in]    ...     書式パラメータ.
 */
#define ERROR(format, ...)                                    \
    do {                                                      \
        fprintf(ERROR_FILE, "%s:%d(%s) " format "\r\n",       \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while (0)

/**
 *  未使用変数の警告抑制マクロ.
 *
 *  @param  [in]    x   未使用変数.
 */
#define UNUSED_VARIABLE(x) (void)(x)

#endif /* __ALCATRAZ_DEBUG_H__ */
