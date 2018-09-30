/** @file   debug.h
 *  @brief  デバッグに関連する機能を提供する.
 *
 *  @author t-kenji <protect.2501@gmail.com>
 *  @date   2018-04-30 新規作成.
 *  @copyright  Copyright (c) 2018 t-kenji
 *
 *  This code is licensed under the MIT License.
 */
#ifndef __ALCATRAZ_DEBUG_H__
#define __ALCATRAZ_DEBUG_H__

#include <stdio.h>

/**
 *  デバッグ出力マクロ.
 *
 *  @param  [in]    format  出力文字列書式.
 *  @param  [in]    ...     書式パラメータ.
 */
#if NODEBUG == 0
#define DEBUG(format, ...)                                                                      \
    do {                                                                                        \
        fprintf(stderr, "%s:%d(%s) " format "\r\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    } while(0)
#else
#define DEBUG(format, ...)
#endif

/**
 *  未使用変数の警告抑制マクロ.
 *
 *  @param  [in]    x   未使用変数.
 */
#define UNUSED_VARIABLE(x) (void)(x)

#endif /* __ALCATRAZ_DEBUG_H__ */
