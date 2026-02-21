/*
  Copyright (c) 2009-2017 Dave Gamble and cJSON contributors

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/* disable warnings about old C89 functions in MSVC */
#if !defined(_CRT_SECURE_NO_DEPRECATE) && defined(_MSC_VER)
#define _CRT_SECURE_NO_DEPRECATE
#endif
/* 
 * GCC编译选项：设置符号可见性为default（默认）
 * 作用：确保cJSON_Utils的公开API能被外部模块访问，私有函数保持隐藏（此处push/pop包裹全局代码）
 */
#ifdef __GNUCC__
#pragma GCC visibility push(default)
#endif
#if defined(_MSC_VER)
#pragma warning (push)
/* disable warning about single line comments in system headers */
#pragma warning (disable : 4001)
#endif
/* 
 * MSVC警告处理：临时禁用4001警告（单行注释）
 * 原因：系统头文件可能包含单行注释，MSVC在严格模式下会警告，不影响功能但影响编译体验
 */
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <math.h>
/* 恢复MSVC警告配置：与前文#pragma warning (push)配对，避免全局禁用警告 */
#if defined(_MSC_VER)
#pragma warning (pop)
#endif
/* 恢复GCC符号可见性配置：与前文#pragma GCC visibility push配对 */
#ifdef __GNUCC__
#pragma GCC visibility pop
#endif
// 引入cJSON_Utils头文件：声明公开API和核心结构体，是工具库的接口约定
#include "cJSON_Utils.h"
/* 
 * 自定义布尔类型：避免不同平台/编译器的bool定义冲突（如C89无原生bool）
 * 重定义true/false为cJSON_bool类型，保证跨平台一致性
 */
/* define our own boolean type */
#ifdef true
#undef true
#endif
#define true ((cJSON_bool)1)

#ifdef false
#undef false
#endif
#define false ((cJSON_bool)0)
/* 
 * 自定义字符串复制函数：替代标准strdup
 * 设计意图：
 * 1. 使用cJSON_malloc而非原生malloc，统一内存分配器（方便内存跟踪/自定义分配）
 * 2. 显式计算长度（strlen+1），+1是为字符串终止符'\0'预留空间
 * 3. 返回unsigned char*，适配后续JSON Pointer的字节级处理
 * 内存管理：调用者需通过cJSON_free释放返回的内存，否则泄漏
 */
static unsigned char* cJSONUtils_strdup(const unsigned char* const string)
{
    size_t length = 0;
    unsigned char *copy = NULL;
// 计算字符串长度：strlen不包含'\0'，+sizeof("")等价于+1，显式预留终止符空间
    length = strlen((const char*)string) + sizeof("");
    copy = (unsigned char*) cJSON_malloc(length);
    if (copy == NULL)// 内存分配失败的防御性检查，C语言必须处理
    {
        return NULL;
    }// 内存拷贝：按计算的长度（含'\0'）复制，保证字符串完整
    memcpy(copy, string, length);

    return copy;
}
/* 
 * 字符串比较函数：修复标准strcmp的NULL处理缺陷
 * 设计意图：
 * 1. NULL指针不视为相等（strcmp(NULL, NULL)会崩溃，此处返回1表示不等）
 * 2. 支持大小写敏感/不敏感比较（满足JSON Pointer的两种比较场景）
 * 3. 指针相等直接返回0（优化，避免不必要的字符遍历）
 * 算法：大小写不敏感时，逐字符转小写比较，直到'\0'或不相等
 */
/* string comparison which doesn't consider NULL pointers equal */
static int compare_strings(const unsigned char *string1, const unsigned char *string2, const cJSON_bool case_sensitive)
{
    // NULL指针特殊处理：任意一个为NULL则返回1（不等），避免访问空指针
    if ((string1 == NULL) || (string2 == NULL))
    {
        return 1;
    }
// 指针相等优化：指向同一内存的字符串必然相等，直接返回0
    if (string1 == string2)
    {
        return 0;
    }
// 大小写敏感：直接调用标准strcmp，效率更高
    if (case_sensitive)
    {
        return strcmp((const char*)string1, (const char*)string2);
    }
// 大小写不敏感：逐字符转小写比较，直到终止符
    for(; tolower(*string1) == tolower(*string2); (void)string1++, string2++)
    {
        // 同时到达终止符，说明字符串相等
        if (*string1 == '\0')
        {
            return 0;
        }
    }
// 返回首个不相等字符的差值（小写），符合strcmp的返回规则
    return tolower(*string1) - tolower(*string2);
}
/* 
 * 浮点数值比较函数：解决直接用==比较浮点数的精度问题
 * 设计意图：
 * 1. 浮点数存储有精度误差（如0.1无法精确表示），不能直接==
 * 2. 使用相对误差（maxVal * DBL_EPSILON），适配不同量级的浮点数
 * 算法：
 * - 取两数绝对值的最大值作为基准
 * - 比较两数差值的绝对值是否≤基准×DBL_EPSILON（双精度浮点的最小精度）
 */
/* securely comparison of floating-point variables */
static cJSON_bool compare_double(double a, double b)
{
    // 取绝对值最大值作为基准，避免小数值的绝对误差误判
    double maxVal = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    // DBL_EPSILON是1.0到下一个可表示双精度数的差值，作为相对误差阈值
    return (fabs(a - b) <= maxVal * DBL_EPSILON);
}
/* 
 * JSON Pointer路径段比较函数：兼容RFC 6901规范
 * 设计意图：
 * 1. JSON Pointer的路径段需处理转义：~0→~，~1→/
 * 2. 比较到下一个'/'或终止符（路径段的边界）
 * 3. 严格校验转义序列，无效转义直接返回false
 */

/* Compare the next path element of two JSON pointers, two NULL pointers are considered unequal: */
static cJSON_bool compare_pointers(const unsigned char *name, const unsigned char *pointer, const cJSON_bool case_sensitive)
{
    // NULL指针不相等（与compare_strings逻辑一致）
    if ((name == NULL) || (pointer == NULL))
    {
        return false;
    }
// 遍历路径段：直到name/pointer终止，或pointer遇到'/'（路径段结束）
    for (; (*name != '\0') && (*pointer != '\0') && (*pointer != '/'); (void)name++, pointer++) /* compare until next '/' */
    {
        if (*pointer == '~')
        {
            // 校验转义序列：~0必须对应~，~1必须对应/，否则转义无效
            /* check for escaped '~' (~0) and '/' (~1) */
            if (((pointer[1] != '0') || (*name != '~')) && ((pointer[1] != '1') || (*name != '/')))
            {
                /* invalid escape sequence or wrong character in *name */
                return false;
            }
            else
            {
                pointer++;// 跳过转义后的字符（0/1），继续比较下一个
            }
        }
            // 字符比较：区分大小写，转义校验通过后比较原字符
        else if ((!case_sensitive && (tolower(*name) != tolower(*pointer))) || (case_sensitive && (*name != *pointer)))
        {
            return false;
        }
    }
     // 边界校验：两个字符串必须同时到达终止符/路径段结束，否则长度不匹配
    if (((*pointer != 0) && (*pointer != '/')) != (*name != 0))
    {
        /* one string has ended, the other not */
        return false;;
    }

    return true;
}
/* 
 * 计算JSON Pointer编码后的字符串长度
 * 设计意图：
 * JSON Pointer规范要求：/→~1，~→~0，每个转义字符会增加1个字节长度
 * 需提前计算长度，避免内存分配不足
 */
/* calculate the length of a string if encoded as JSON pointer with ~0 and ~1 escape sequences */
static size_t pointer_encoded_length(const unsigned char *string)
{
    size_t length;
    // 遍历字符串，遇到/或~则长度+1（转义占用额外字节）
    for (length = 0; *string != '\0'; (void)string++, length++)
    {
        /* character needs to be escaped? */
        if ((*string == '~') || (*string == '/'))
        {
            length++;
        }
    }

    return length;
}
/* 
 * 将字符串编码为JSON Pointer格式（原地/逐字符处理）
 * 设计意图：
 * 严格遵循RFC 6901：
 * - '/' → ~1（路径分隔符转义）
 * - '~' → ~0（转义符自身转义）
 * 内存：destination需提前分配足够空间（通过pointer_encoded_length计算）
 */
/* copy a string while escaping '~' and '/' with ~0 and ~1 JSON pointer escape codes */
static void encode_string_as_pointer(unsigned char *destination, const unsigned char *source)
{
    // 逐字符编码，直到source终止
    for (; source[0] != '\0'; (void)source++, destination++)
    {
        if (source[0] == '/')
        {
            destination[0] = '~';
            destination[1] = '1';
            destination++; // 跳过第二个转义字符，继续
        }
        else if (source[0] == '~')
        {
            destination[0] = '~';
            destination[1] = '0';
            destination++;
        }
        else
        {
            destination[0] = source[0];
        }
    }

    destination[0] = '\0';// 字符串终止符，保证编码后字符串完整
}
/* 
 * 公开API：查找从源JSON对象到目标节点的JSON Pointer路径
 * 设计意图：
 * 1. 递归遍历JSON的子节点（数组/对象），生成符合RFC 6901的Pointer路径
 * 2. 数组节点用索引（/0/1），对象节点用编码后的键（转义/和~）
 * 内存管理：返回的字符串需调用cJSON_free释放，递归过程中分配的临时内存需及时释放
 */
CJSON_PUBLIC(char *) cJSONUtils_FindPointerFromObjectTo(const cJSON * const object, const cJSON * const target)
{
    size_t child_index = 0;
    cJSON *current_child = 0;

    if ((object == NULL) || (target == NULL))
    {
        return NULL;
    }
// 源对象就是目标节点：返回空字符串（表示根节点）
    if (object == target)
    {
        /* found */
        return (char*)cJSONUtils_strdup((const unsigned char*)"");
    }
// 递归遍历所有子节点：数组/对象的child链表
    /* recursively search all children of the object or array */
    for (current_child = object->child; current_child != NULL; (void)(current_child = current_child->next), child_index++)
    {
        // 递归查找：当前子节点到目标节点的路径
        unsigned char *target_pointer = (unsigned char*)cJSONUtils_FindPointerFromObjectTo(current_child, target);
        /* found the target? */
        if (target_pointer != NULL)// 找到目标节点，拼接路径
        {
            if (cJSON_IsArray(object))// 源是数组：路径格式 /<索引><子路径>
            {
                /* reserve enough memory for a 64 bit integer + '/' and '\0' */
                unsigned char *full_pointer = (unsigned char*)cJSON_malloc(strlen((char*)target_pointer) + 20 + sizeof("/"));
                
                // 溢出检查：size_t转unsigned long，避免索引超出ULONG_MAX（跨平台兼容）
                /* check if conversion to unsigned long is valid
                 * This should be eliminated at compile time by dead code elimination
                 * if size_t is an alias of unsigned long, or if it is bigger */
                if (child_index > ULONG_MAX)
                {
                    cJSON_free(target_pointer);
                    cJSON_free(full_pointer);
                    return NULL;
                }
                 // 格式化路径：/索引+子路径（如/0/name）
                sprintf((char*)full_pointer, "/%lu%s", (unsigned long)child_index, target_pointer); /* /<array_index><path> */
                cJSON_free(target_pointer);

                return (char*)full_pointer;
            }

            if (cJSON_IsObject(object))// 源是对象：路径格式 /<编码键><子路径>
            {
                // 内存分配：子路径长度 + 编码后键长度 + 2（/和'\0'）
                unsigned char *full_pointer = (unsigned char*)cJSON_malloc(strlen((char*)target_pointer) + pointer_encoded_length((unsigned char*)current_child->string) + 2);
                full_pointer[0] = '/';
                // 编码对象键为JSON Pointer格式（转义/和~）
                encode_string_as_pointer(full_pointer + 1, (unsigned char*)current_child->string);
                // 拼接子路径（如/name/age
                strcat((char*)full_pointer, (char*)target_pointer);
                cJSON_free(target_pointer);

                return (char*)full_pointer;
            }
            // 非数组/对象（叶子节点）：未找到目标，释放内存返回NULL
            /* reached leaf of the tree, found nothing */
            cJSON_free(target_pointer);
            return NULL;
        }
    }

    // 遍历完所有子节点未找到：返回NULL
    /* not found */
    return NULL;
}
/* 
 * 修复版数组元素获取函数：替代原生cJSON_GetArrayItem
 * 设计意图：
 * 原生cJSON_GetArrayItem可能存在越界处理不严谨的问题，此处：
 * 1. 先判空array，避免访问空指针的child
 * 2. 循环递减索引，遍历child链表，直到索引为0或child为NULL
 * 优势：更健壮的边界处理，避免数组越界导致的崩溃
 */
/* non broken version of cJSON_GetArrayItem */
static cJSON *get_array_item(const cJSON *array, size_t item)
{
    cJSON *child = array ? array->child : NULL;// 三元运算符判空，避免空指针
    while ((child != NULL) && (item > 0))
    {
        item--;
        child = child->next;
    }

    return child;
}
/* 
 * 从JSON Pointer解析数组索引：严格遵循RFC 6901
 * 设计意图：
 * 1. 禁止前导零（如012是无效索引），符合JSON Pointer规范
 * 2. 仅解析数字字符，遇到非数字/终止符/路径分隔符'/'停止
 * 3. 输出索引到*index，返回是否解析成功
 */
static cJSON_bool decode_array_index_from_pointer(const unsigned char * const pointer, size_t * const index)
{
    size_t parsed_index = 0;
    size_t position = 0;
// 前导零校验：0后接非终止符/非/则无效（如01、00）
    if ((pointer[0] == '0') && ((pointer[1] != '\0') && (pointer[1] != '/')))
    {
        /* leading zeroes are not permitted */
        return 0;
    }
// 逐字符解析数字：仅0-9，避免非数字索引（如/abc）
    for (position = 0; (pointer[position] >= '0') && (pointer[position] <= '9'); position++)
    {
        parsed_index = (10 * parsed_index) + (size_t)(pointer[position] - '0');

    }
// 终止校验：解析停止后，指针必须指向终止符或/，否则包含非数字字符
    if ((pointer[position] != '\0') && (pointer[position] != '/'))
    {
        return 0;
    }

    *index = parsed_index;// 输出解析后的索引

    return 1;
}
/* 
 * 核心函数：根据JSON Pointer查找JSON节点
 * 设计意图：
 * 1. 支持数组（按索引）、对象（按键，区分大小写）查找
 * 2. 逐段解析Pointer路径（以/分隔），递归式遍历JSON结构
 * 3. 兼容RFC 6901的转义规则
 */
static cJSON *get_item_from_pointer(cJSON * const object, const char * pointer, const cJSON_bool case_sensitive)
{
    cJSON *current_element = object;

    if (pointer == NULL)
    {
        return NULL;
    }

    // 遍历Pointer路径：每次处理一个/分隔的路径段
    /* follow path of the pointer */
    while ((pointer[0] == '/') && (current_element != NULL))
    {
        pointer++;// 跳过路径分隔符/，指向路径段开头
        if (cJSON_IsArray(current_element))// 当前节点是数组：按索引查找
        {
            size_t index = 0;
            // 解析数组索引，失败则返回NULL
            if (!decode_array_index_from_pointer((const unsigned char*)pointer, &index))
            {
                return NULL;
            }

            current_element = get_array_item(current_element, index);
        }
        else if (cJSON_IsObject(current_element))// 当前节点是对象：按键查找
        {
            current_element = current_element->child;
            // 遍历对象的child链表，用compare_pointers匹配键（支持转义）
            /* GetObjectItem. */
            while ((current_element != NULL) && !compare_pointers((unsigned char*)current_element->string, (const unsigned char*)pointer, case_sensitive))
            {
                current_element = current_element->next;
            }
        }
        else// 非数组/对象（叶子节点）：无法继续查找
        {
            return NULL;
        }
        // 跳过当前路径段：指向下一个/或终止符，准备处理下一段
        /* skip to the next path token or end of string */
        while ((pointer[0] != '\0') && (pointer[0] != '/'))
        {
            pointer++;
        }
    }

    return current_element;
}
/* 
 * 公开API：大小写不敏感的JSON Pointer查找
 * 设计意图：封装get_item_from_pointer，默认大小写不敏感（更通用）
 */

CJSON_PUBLIC(cJSON *) cJSONUtils_GetPointer(cJSON * const object, const char *pointer)
{
    return get_item_from_pointer(object, pointer, false);
}
/* 
 * 公开API：大小写敏感的JSON Pointer查找
 * 设计意图：满足严格的键名匹配场景（如JSON对象键区分大小写）
 */
CJSON_PUBLIC(cJSON *) cJSONUtils_GetPointerCaseSensitive(cJSON * const object, const char *pointer)
{
    return get_item_from_pointer(object, pointer, true);
}
/* 
 * JSON Pointer解码函数：原地转义还原（~0→~，~1→/）
 * 设计意图：
 * 1. 原地修改（decoded_string=string），节省内存（无需额外分配）
 * 2. 遇到无效转义（~后非0/1）直接返回，避免错误解析
 * 注意：调用者需确保string可写，且长度足够
 */
/* JSON Patch implementation. */
static void decode_pointer_inplace(unsigned char *string)
{
    unsigned char *decoded_string = string;

    if (string == NULL) {
        return;
    }

    // 逐字符解码，直到string终止
    for (; *string; (void)decoded_string++, string++)
    {
        if (string[0] == '~')
        {
            if (string[1] == '0')
            {
                decoded_string[0] = '~';
            }
            else if (string[1] == '1')
            {
                decoded_string[1] = '/';
            }
            else// 无效转义序列，直接返回
            {
                /* invalid escape sequence */
                return;
            }

            string++;
        }
    }

    decoded_string[0] = '\0';
}
/* 
 * 修复版数组元素分离函数：替代原生cJSON_DetachItemFromArray
 * 设计意图：
 * 原生函数可能未正确处理链表的prev/next指针，此处：
 * 1. 遍历找到目标元素，处理链表的前驱/后继指针
 * 2. 分离后将元素的prev/next置NULL，避免野指针
 * 内存管理：分离的元素不会被释放，调用者需自行处理（Delete/Add）
 */
/* non-broken cJSON_DetachItemFromArray */
static cJSON *detach_item_from_array(cJSON *array, size_t which)
{
    cJSON *c = array->child;
    while (c && (which > 0))
    {
        c = c->next;
        which--;
    }
    if (!c)// 索引越界，无此元素
    {
        /* item doesn't exist */
        return NULL;
    }
    // 处理前驱指针：非首元素则前驱的next指向当前元素的next
    if (c != array->child)
    {
        /* not the first element */
        c->prev->next = c->next;
    }
    // 处理后继指针：后继的prev指向前驱
    if (c->next)
    {
        c->next->prev = c->prev;
    }
     // 处理数组的child指针：若分离首元素，child指向后继
    if (c == array->child)
    {
        array->child = c->next;
    }
    else if (c->next == NULL)// 分离尾元素，更新尾指针（兼容链表结构）
    {
        array->child->prev = c->prev;
    }
    // 清空分离元素的指针：避免野指针引用原数组
    /* make sure the detached item doesn't point anywhere anymore */
    c->prev = c->next = NULL;

    return c;
}
/* 
 * 根据JSON Pointer分离节点：从父节点中移除目标节点（不释放）
 * 设计意图：
 * 1. 拆分Pointer为父路径和子路径（最后一个/分隔）
 * 2. 解码子路径，根据父节点类型（数组/对象）分离元素
 * 3. 内存管理：临时路径字符串需释放，分离的元素由调用者释放
 */
/* detach an item at the given path */
static cJSON *detach_path(cJSON *object, const unsigned char *path, const cJSON_bool case_sensitive)
{
    unsigned char *parent_pointer = NULL;
    unsigned char *child_pointer = NULL;
    cJSON *parent = NULL;
    cJSON *detached_item = NULL;
// 复制路径：后续拆分父/子路径，避免修改原路径
    /* copy path and split it in parent and child */
    parent_pointer = cJSONUtils_strdup(path);
    if (parent_pointer == NULL) {
        goto cleanup;
    }
// 查找最后一个/：拆分父路径（到/为止）和子路径（/之后）
    child_pointer = (unsigned char*)strrchr((char*)parent_pointer, '/'); /* last '/' */
    if (child_pointer == NULL)
    {
        goto cleanup;
    }
    // 拆分字符串：父路径终止符置\0，子指针指向/后
    /* split strings */
    child_pointer[0] = '\0';
    child_pointer++;
// 查找父节点：根据父路径找到目标节点的父节点
    parent = get_item_from_pointer(object, (char*)parent_pointer, case_sensitive);
    // 解码子路径（还原转义符）
    decode_pointer_inplace(child_pointer);
// 根据父节点类型分离子节点
    if (cJSON_IsArray(parent))// 父是数组：按索引分离
    {
        size_t index = 0;
        if (!decode_array_index_from_pointer(child_pointer, &index))
        {
            goto cleanup;
        }
        detached_item = detach_item_from_array(parent, index);
    }
    else if (cJSON_IsObject(parent))// 父是对象：按键分离
    {
        detached_item = cJSON_DetachItemFromObject(parent, (char*)child_pointer);
    }
    else// 父节点非数组/对象，无法分离
    {
        /* Couldn't find object to remove child from. */
        goto cleanup;
    }

cleanup:// 统一清理逻辑：释放临时路径内存
    if (parent_pointer != NULL)
    {
        cJSON_free(parent_pointer);
    }

    return detached_item;
}
/* 
 * 链表归并排序函数：用于JSON对象的键排序
 * 设计意图：
 * 1. 归并排序（Merge Sort）：稳定排序，适合链表（无需随机访问）
 * 2. 递归拆分链表为两半，排序后合并
 * 3. 支持大小写敏感/不敏感比较（通过compare_strings）
 * 算法步骤：
 * - 基线条件：空/单节点链表已排序
 * - 提前校验：链表已排序则直接返回（优化）
 * - 快慢指针找链表中点，拆分
 * - 递归排序子链表
 * - 合并两个有序子链表
 */
/* sort lists using mergesort */
static cJSON *sort_list(cJSON *list, const cJSON_bool case_sensitive)
{
    cJSON *first = list;
    cJSON *second = list;
    cJSON *current_item = list;
    cJSON *result = list;
    cJSON *result_tail = NULL;
// 基线条件：空链表或单节点链表无需排序
    if ((list == NULL) || (list->next == NULL))
    {
        /* One entry is sorted already. */
        return result;
    }
// 提前优化：检查链表是否已排序，已排序则直接返回
    while ((current_item != NULL) && (current_item->next != NULL) && (compare_strings((unsigned char*)current_item->string, (unsigned char*)current_item->next->string, case_sensitive) < 0))
    {
        /* Test for list sorted. */
        current_item = current_item->next;
    }
    if ((current_item == NULL) || (current_item->next == NULL))
    {
        /* Leave sorted lists unmodified. */
        return result;
    }
// 重置指针，准备拆分链表
    /* reset pointer to the beginning */
    current_item = list;
    // 快慢指针找中点：second走1步，current_item走2步，最终second指向中点
    while (current_item != NULL)
    {
        /* Walk two pointers to find the middle. */
        second = second->next;
        current_item = current_item->next;
        /* advances current_item two steps at a time */
        if (current_item != NULL)
        {
            current_item = current_item->next;
        }
    }
    // 拆分链表：切断中点的前驱指针，分为first和second两个子链表
    if ((second != NULL) && (second->prev != NULL))
    {
        /* Split the lists */
        second->prev->next = NULL;
        second->prev = NULL;
    }
// 递归排序两个子链表
    /* Recursively sort the sub-lists. */
    first = sort_list(first, case_sensitive);
    second = sort_list(second, case_sensitive);
    result = NULL;

    // 合并两个有序子链表
    /* Merge the sub-lists */
    while ((first != NULL) && (second != NULL))
    {
        cJSON *smaller = NULL;
        // 比较两个节点的键，选择较小的节点
        if (compare_strings((unsigned char*)first->string, (unsigned char*)second->string, case_sensitive) < 0)
        {
            smaller = first;
        }
        else// 将较小节点加入合并链表
        {
            smaller = second;
        }
// 移动选中节点的指针，继续合并
        if (result == NULL)
        {
            /* start merged list with the smaller element */
            result_tail = smaller;
            result = smaller;
        }
        else
        {
            /* add smaller element to the list */
            result_tail->next = smaller;
            smaller->prev = result_tail;
            result_tail = smaller;
        }

        if (first == smaller)
        {
            first = first->next;
        }
        else
        {
            second = second->next;
        }
    }
// 拼接剩余的子链表（first/second必有一个为空）
    if (first != NULL)
    {
        /* Append rest of first list. */
        if (result == NULL)
        {
            return first;
        }
        result_tail->next = first;
        first->prev = result_tail;
    }
    if (second != NULL)
    {
        /* Append rest of second list */
        if (result == NULL)
        {
            return second;
        }
        result_tail->next = second;
        second->prev = result_tail;
    }

    return result;
}
/* 
 * JSON对象排序函数：排序对象的子节点（按键名）
 * 设计意图：
 * JSON对象的键是无序的，但比较/生成补丁时需要有序，因此排序child链表
 */
static void sort_object(cJSON * const object, const cJSON_bool case_sensitive)
{
    if (object == NULL)
    {
        return;
    }
    // 排序对象的child链表（键值对链表）
    object->child = sort_list(object->child, case_sensitive);
}
/* 
 * 递归比较两个JSON节点是否完全相同
 * 设计意图：
 * 1. 先比较类型，类型不同直接不等
 * 2. 按类型递归比较值：数字（int+double）、字符串、数组（逐元素）、对象（排序后逐键）
 * 3. 数组/对象需递归比较子节点
 */
static cJSON_bool compare_json(cJSON *a, cJSON *b, const cJSON_bool case_sensitive)
{
    // 类型校验：指针空/类型不同，直接不等
    if ((a == NULL) || (b == NULL) || ((a->type & 0xFF) != (b->type & 0xFF)))
    {
        /* mismatched type. */
        return false;
    }
    switch (a->type & 0xFF)
    {
        case cJSON_Number:// 数字类型：同时比较int和double（兼容整数/浮点存储）
            /* numeric mismatch. */
            if ((a->valueint != b->valueint) || (!compare_double(a->valuedouble, b->valuedouble)))
            {
                return false;
            }
            else
            {
                return true;
            }

        case cJSON_String:// 字符串类型：直接比较valuestring
            /* string mismatch. */
            if (strcmp(a->valuestring, b->valuestring) != 0)
            {
                return false;
            }
            else
            {
                return true;
            }

        case cJSON_Array:// 数组类型：逐元素递归比较
            for ((void)(a = a->child), b = b->child; (a != NULL) && (b != NULL); (void)(a = a->next), b = b->next)
            {
                cJSON_bool identical = compare_json(a, b, case_sensitive);
                if (!identical)
                {
                    return false;
                }
            }
// 数组长度校验：必须同时遍历完，否则长度不同
            /* array size mismatch? (one of both children is not NULL) */
            if ((a != NULL) || (b != NULL))
            {
                return false;
            }
            else
            {
                return true;
            }

        case cJSON_Object:// 对象类型：先排序，再逐键比较
            sort_object(a, case_sensitive);
            sort_object(b, case_sensitive);
            for ((void)(a = a->child), b = b->child; (a != NULL) && (b != NULL); (void)(a = a->next), b = b->next)
            {
                cJSON_bool identical = false;
                /* compare object keys */
                if (compare_strings((unsigned char*)a->string, (unsigned char*)b->string, case_sensitive))
                {
                    /* missing member */
                    return false;
                }
                // 键值递归比较
                identical = compare_json(a, b, case_sensitive);
                if (!identical)
                {
                    return false;
                }
            }
// 对象长度校验：必须同时遍历完
            /* object length mismatch (one of both children is not null) */
            if ((a != NULL) || (b != NULL))
            {
                return false;
            }
            else
            {
                return true;
            }

        default: // 布尔/NULL类型：类型相同则值相同
            break;
    }

    /* null, true or false */
    return true;
}
/* 
 * 修复版数组元素插入函数：替代原生cJSON_InsertItemInArray
 * 设计意图：
 * 原生函数可能存在边界处理缺陷，此处：
 * 1. 处理索引越界（which>数组长度）返回0
 * 2. 正确处理链表的prev/next指针，支持插入到任意位置（开头/中间/末尾）
 */
/* non broken version of cJSON_InsertItemInArray */
static cJSON_bool insert_item_in_array(cJSON *array, size_t which, cJSON *newitem)
{
    cJSON *child = array->child;
    // 遍历找到插入位置的前驱节点
    while (child && (which > 0))
    {
        child = child->next;
        which--;
    }
    if (which > 0)// 索引越界（大于数组长度），插入失败
    {
        /* item is after the end of the array */
        return 0;
    }
    if (child == NULL)// 插入到数组末尾（等价于AddItemToArray）
    {
        cJSON_AddItemToArray(array, newitem);
        return 1;
    }
// 插入到链表中间/开头：调整prev/next指针
    /* insert into the linked list */
    newitem->next = child;
    newitem->prev = child->prev;
    child->prev = newitem;
// 处理开头插入：更新数组的child指针
    /* was it at the beginning */
    if (child == array->child)
    {
        array->child = newitem;
    }
    else// 处理中间插入：更新前驱的next指针
    {
        newitem->prev->next = newitem;
    }

    return 1;
}
/* 
 * 封装对象键查找：支持大小写敏感/不敏感
 * 设计意图：统一查找逻辑，减少代码冗余
 */
static cJSON *get_object_item(const cJSON * const object, const char* name, const cJSON_bool case_sensitive)
{
    if (case_sensitive)
    {
        return cJSON_GetObjectItemCaseSensitive(object, name);
    }

    return cJSON_GetObjectItem(object, name);
}
/* 
 * JSON Patch操作类型枚举：对应RFC 6902的6种操作
 * INVALID：无效操作，用于错误处理
 */
enum patch_operation { INVALID, ADD, REMOVE, REPLACE, MOVE, COPY, TEST };
/* 
 * 解析JSON Patch的操作类型（op字段）
 * 设计意图：
 * 1. 校验op字段是字符串类型
 * 2. 映射字符串到枚举值，无效操作返回INVALID
 */
static enum patch_operation decode_patch_operation(const cJSON * const patch, const cJSON_bool case_sensitive)
{
    cJSON *operation = get_object_item(patch, "op", case_sensitive);
    if (!cJSON_IsString(operation))
    {
        return INVALID;
    }
// 映射op字符串到枚举值
    if (strcmp(operation->valuestring, "add") == 0)
    {
        return ADD;
    }

    if (strcmp(operation->valuestring, "remove") == 0)
    {
        return REMOVE;
    }

    if (strcmp(operation->valuestring, "replace") == 0)
    {
        return REPLACE;
    }

    if (strcmp(operation->valuestring, "move") == 0)
    {
        return MOVE;
    }

    if (strcmp(operation->valuestring, "copy") == 0)
    {
        return COPY;
    }

    if (strcmp(operation->valuestring, "test") == 0)
    {
        return TEST;
    }

    return INVALID;
}
/* 
 * 覆盖JSON节点：释放原有资源，替换为新节点
 * 设计意图：
 * 1. 释放原节点的string（键名）、valuestring（字符串值）、child（子节点）
 * 2. 用memcpy覆盖节点内存，避免重新分配节点
 * 内存管理：必须先释放原有资源，否则内存泄漏
 */
/* overwrite and existing item with another one and free resources on the way */
static void overwrite_item(cJSON * const root, const cJSON replacement)
{
    if (root == NULL)
    {
        return;
    }
// 释放原节点的动态内存
    if (root->string != NULL)
    {
        cJSON_free(root->string);
    }
    if (root->valuestring != NULL)
    {
        cJSON_free(root->valuestring);
    }
    if (root->child != NULL)
    {
        cJSON_Delete(root->child);// 递归删除子节点
    }
// 覆盖节点数据：memcpy复制replacement的所有字段
    memcpy(root, &replacement, sizeof(cJSON));
}
/* 
 * 应用单个JSON Patch操作：核心实现（RFC 6902）
 * 设计意图：
 * 1. 支持add/remove/replace/move/copy/test 6种操作
 * 2. 处理根节点替换、路径解析、内存分配/释放、错误码返回
 * 错误码设计：不同错误返回不同int值，方便调用者定位问题
 */
static int apply_patch(cJSON *object, const cJSON *patch, const cJSON_bool case_sensitive)
{
    cJSON *path = NULL;
    cJSON *value = NULL;
    cJSON *parent = NULL;
    enum patch_operation opcode = INVALID;
    unsigned char *parent_pointer = NULL;
    unsigned char *child_pointer = NULL;
    int status = 0;
// 解析patch的path字段：必须是字符串
    path = get_object_item(patch, "path", case_sensitive);
    if (!cJSON_IsString(path))
    {
        /* malformed patch. */
        status = 2;
        goto cleanup;
    }

    opcode = decode_patch_operation(patch, case_sensitive);
    if (opcode == INVALID)
    {
        status = 3;
        goto cleanup;
    }
    else if (opcode == TEST)
    {
        /* compare value: {...} with the given path */
        status = !compare_json(get_item_from_pointer(object, path->valuestring, case_sensitive), get_object_item(patch, "value", case_sensitive), case_sensitive);
        goto cleanup;
    }

    // 特殊处理：替换根节点（path为空字符串）
    /* special case for replacing the root */
    if (path->valuestring[0] == '\0')
    {
        if (opcode == REMOVE)
        {
            static const cJSON invalid = { NULL, NULL, NULL, cJSON_Invalid, NULL, 0, 0, NULL};

            overwrite_item(object, invalid);

            status = 0;
            goto cleanup;
        }

        if ((opcode == REPLACE) || (opcode == ADD))
        {
            // 复制value节点（深拷贝）
            value = get_object_item(patch, "value", case_sensitive);
            if (value == NULL)
            {
                /* missing "value" for add/replace. */
                status = 7;
                goto cleanup;
            }

            value = cJSON_Duplicate(value, 1);
            if (value == NULL)
            {
                /* out of memory for add/replace. */
                status = 8;
                goto cleanup;
            }

            overwrite_item(object, *value);

            /* delete the duplicated value */
            cJSON_free(value);
            value = NULL;

            /* the string "value" isn't needed */
            if (object->string != NULL)
            {
                cJSON_free(object->string);
                object->string = NULL;
            }

            status = 0;
            goto cleanup;
        }
    }
// REMOVE/REPLACE操作：先删除旧节点
    if ((opcode == REMOVE) || (opcode == REPLACE))
    {
        /* Get rid of old. */
        cJSON *old_item = detach_path(object, (unsigned char*)path->valuestring, case_sensitive);
        if (old_item == NULL)
        {
            status = 13;
            goto cleanup;
        }
        cJSON_Delete(old_item);
        if (opcode == REMOVE)
        {
            /* For Remove, this job is done. */
            status = 0;
            goto cleanup;
        }
    }
// MOVE/COPY操作：从from路径获取节点
    /* Copy/Move uses "from". */
    if ((opcode == MOVE) || (opcode == COPY))
    {
        cJSON *from = get_object_item(patch, "from", case_sensitive);
        if (from == NULL)
        {
            /* missing "from" for copy/move. */
            status = 4;
            goto cleanup;
        }

        if (opcode == MOVE)
        {
            value = detach_path(object, (unsigned char*)from->valuestring, case_sensitive);
        }
        if (opcode == COPY)// COPY：深拷贝节点（避免原节点被修改）
        {
            value = get_item_from_pointer(object, from->valuestring, case_sensitive);
        }
        if (value == NULL)
        {
            /* missing "from" for copy/move. */
            status = 5;
            goto cleanup;
        }
        if (opcode == COPY)
        {
            value = cJSON_Duplicate(value, 1);
        }
        if (value == NULL)
        {
            /* out of memory for copy/move. */
            status = 6;
            goto cleanup;
        }
    }
    else /* Add/Replace uses "value". */
        // ADD/REPLACE操作：从value字段获取节点
    {
        value = get_object_item(patch, "value", case_sensitive);
        if (value == NULL)
        {
            /* missing "value" for add/replace. */
            status = 7;
            goto cleanup;
        }
        value = cJSON_Duplicate(value, 1);
        if (value == NULL)
        {
            /* out of memory for add/replace. */
            status = 8;
            goto cleanup;
        }
    }

    /* Now, just add "value" to "path". */
// 拆分path为父路径和子路径，准备添加节点
    /* split pointer in parent and child */
    parent_pointer = cJSONUtils_strdup((unsigned char*)path->valuestring);
    if (parent_pointer) {
        child_pointer = (unsigned char*)strrchr((char*)parent_pointer, '/');
    }
    if (child_pointer != NULL)
    {
        child_pointer[0] = '\0';
        child_pointer++;
    }
    // 查找父节点，解码子路径
    parent = get_item_from_pointer(object, (char*)parent_pointer, case_sensitive);
    decode_pointer_inplace(child_pointer);

    /* add, remove, replace, move, copy, test. */
    if ((parent == NULL) || (child_pointer == NULL))
    {
        /* Couldn't find object to add to. */
        status = 9;
        goto cleanup;
    }
    else if (cJSON_IsArray(parent))
    {
        if (strcmp((char*)child_pointer, "-") == 0)
        {
            cJSON_AddItemToArray(parent, value);
            value = NULL;
        }
        else
        {
            size_t index = 0;
            if (!decode_array_index_from_pointer(child_pointer, &index))
            {
                status = 11;
                goto cleanup;
            }

            if (!insert_item_in_array(parent, index, value))
            {
                status = 10;
                goto cleanup;
            }
            value = NULL;
        }
    }
    else if (cJSON_IsObject(parent))// 父是对象：添加/替换键值对
    {
        if (case_sensitive)
        {
            cJSON_DeleteItemFromObjectCaseSensitive(parent, (char*)child_pointer);
        }
        else
        {
            cJSON_DeleteItemFromObject(parent, (char*)child_pointer);
        }
        cJSON_AddItemToObject(parent, (char*)child_pointer, value);
        value = NULL;
    }
    else /* parent is not an object */// 父节点非数组/对象
    {
        /* Couldn't find object to add to. */
        status = 9;
        goto cleanup;
    }

cleanup:// 统一清理逻辑：释放未转移所有权的value和临时路径
    if (value != NULL)
    {
        cJSON_Delete(value);
    }
    if (parent_pointer != NULL)
    {
        cJSON_free(parent_pointer);
    }

    return status;
}
/* 
 * 公开API：应用JSON Patch数组（大小写不敏感）
 * 设计意图：
 * 1. 校验patches是数组（RFC 6902要求）
 * 2. 遍历每个patch，逐个应用，失败则返回错误码
 */
CJSON_PUBLIC(int) cJSONUtils_ApplyPatches(cJSON * const object, const cJSON * const patches)
{
    const cJSON *current_patch = NULL;
    int status = 0;

    if (!cJSON_IsArray(patches))
    {
        /* malformed patches. */
        return 1;
    }

    if (patches != NULL)
    {
        current_patch = patches->child;
    }
// 遍历所有patch，逐个应用
    while (current_patch != NULL)
    {
        status = apply_patch(object, current_patch, false);
        if (status != 0)
        {
            return status;
        }
        current_patch = current_patch->next;
    }

    return 0;// 所有patch应用成功
}
/* 
 * 公开API：应用JSON Patch数组（大小写敏感）
 * 设计意图：满足严格的键名匹配场景
 */
CJSON_PUBLIC(int) cJSONUtils_ApplyPatchesCaseSensitive(cJSON * const object, const cJSON * const patches)
{
    const cJSON *current_patch = NULL;
    int status = 0;

    if (!cJSON_IsArray(patches))
    {
        /* malformed patches. */
        return 1;
    }

    if (patches != NULL)
    {
        current_patch = patches->child;
    }

    while (current_patch != NULL)
    {
        status = apply_patch(object, current_patch, true);
        if (status != 0)
        {
            return status;
        }
        current_patch = current_patch->next;
    }

    return 0;
}
/* 
 * 构建单个JSON Patch操作对象
 * 设计意图：
 * 1. 封装patch的创建逻辑，支持路径拼接（suffix）
 * 2. 编码路径为JSON Pointer格式，复制value节点（深拷贝）
 */
static void compose_patch(cJSON * const patches, const unsigned char * const operation, const unsigned char * const path, const unsigned char *suffix, const cJSON * const value)
{
    cJSON *patch = NULL;
// 空指针防御：关键参数为空则返回
    if ((patches == NULL) || (operation == NULL) || (path == NULL))
    {
        return;
    }
// 创建patch对象（{op:..., path:..., value:...}）
    patch = cJSON_CreateObject();
    if (patch == NULL)
    {
        return;
    }
    cJSON_AddItemToObject(patch, "op", cJSON_CreateString((const char*)operation));
// 处理路径：无suffix则直接用path，有则拼接并编码
    if (suffix == NULL)
    {
        cJSON_AddItemToObject(patch, "path", cJSON_CreateString((const char*)path));
    }
    else
    {
        // 计算拼接后路径的长度：原路径 + 编码后suffix + / + '\0'
        size_t suffix_length = pointer_encoded_length(suffix);
        size_t path_length = strlen((const char*)path);
        unsigned char *full_path = (unsigned char*)cJSON_malloc(path_length + suffix_length + sizeof("/"));
// 拼接路径前缀（path/）
        sprintf((char*)full_path, "%s/", (const char*)path);
        // 编码suffix为JSON Pointer格式，拼接到路径后
        encode_string_as_pointer(full_path + path_length + 1, suffix);

        cJSON_AddItemToObject(patch, "path", cJSON_CreateString((const char*)full_path));
        cJSON_free(full_path);
    }
// 添加value字段（深拷贝，避免原节点被修改）
    if (value != NULL)
    {
        cJSON_AddItemToObject(patch, "value", cJSON_Duplicate(value, 1));
    }
    cJSON_AddItemToArray(patches, patch);
}
/* 
 * 公开API：向补丁数组添加单个patch操作
 * 设计意图：简化patch创建，封装compose_patch，suffix为NULL
 */
CJSON_PUBLIC(void) cJSONUtils_AddPatchToArray(cJSON * const array, const char * const operation, const char * const path, const cJSON * const value)
{
    compose_patch(array, (const unsigned char*)operation, (const unsigned char*)path, NULL, value);
}
/* 
 * 递归生成两个JSON的差异补丁（JSON Patch）
 * 设计意图：
 * 1. 对比from和to两个JSON，生成将from转为to的patch数组
 * 2. 处理类型不同、值不同、数组/对象的增删改
 * 3. 严格遵循RFC 6902规范
 */
static void create_patches(cJSON * const patches, const unsigned char * const path, cJSON * const from, cJSON * const to, const cJSON_bool case_sensitive)
{
    if ((from == NULL) || (to == NULL))
    {
        return;
    }
// 类型不同：直接replace为to的值
    if ((from->type & 0xFF) != (to->type & 0xFF))
    {
        compose_patch(patches, (const unsigned char*)"replace", path, 0, to);
        return;
    }

    switch (from->type & 0xFF)
    {
        case cJSON_Number:
            if ((from->valueint != to->valueint) || !compare_double(from->valuedouble, to->valuedouble))
            {
                compose_patch(patches, (const unsigned char*)"replace", path, NULL, to);
            }
            return;

        case cJSON_String:// 字符串类型：值不同则replace
            if (strcmp(from->valuestring, to->valuestring) != 0)
            {
                compose_patch(patches, (const unsigned char*)"replace", path, NULL, to);
            }
            return;

        case cJSON_Array:// 数组类型：逐元素对比，处理增删改
        {
            size_t index = 0;
            cJSON *from_child = from->child;
            cJSON *to_child = to->child;
            // 分配路径内存：预留20字节给64位索引
            unsigned char *new_path = (unsigned char*)cJSON_malloc(strlen((const char*)path) + 20 + sizeof("/")); /* Allow space for 64bit int. log10(2^64) = 20 */
// 对比共同长度的元素：递归生成replace补丁
            /* generate patches for all array elements that exist in both "from" and "to" */
            for (index = 0; (from_child != NULL) && (to_child != NULL); (void)(from_child = from_child->next), (void)(to_child = to_child->next), index++)
            {
                /* check if conversion to unsigned long is valid
                 * This should be eliminated at compile time by dead code elimination
                 * if size_t is an alias of unsigned long, or if it is bigger */
              // 溢出检查：size_t转unsigned long
                if (index > ULONG_MAX)
                {
                    cJSON_free(new_path);
                    return;
                }
                sprintf((char*)new_path, "%s/%lu", path, (unsigned long)index); /* path of the current array element */
                create_patches(patches, new_path, from_child, to_child, case_sensitive);
            }

            /* remove leftover elements from 'from' that are not in 'to' */
            // from有多余元素：生成remove补丁
            for (; (from_child != NULL); (void)(from_child = from_child->next))
            {
                /* check if conversion to unsigned long is valid
                 * This should be eliminated at compile time by dead code elimination
                 * if size_t is an alias of unsigned long, or if it is bigger */
                if (index > ULONG_MAX)
                {
                    cJSON_free(new_path);
                    return;
                }
                sprintf((char*)new_path, "%lu", (unsigned long)index);
                compose_patch(patches, (const unsigned char*)"remove", path, new_path, NULL);
            }
            /* add new elements in 'to' that were not in 'from' */
            // to有新增元素：生成add补丁（-表示添加到末尾）
            for (; (to_child != NULL); (void)(to_child = to_child->next), index++)
            {
                compose_patch(patches, (const unsigned char*)"add", path, (const unsigned char*)"-", to_child);
            }
            cJSON_free(new_path);
            return;
        }

        case cJSON_Object:
        {
            cJSON *from_child = NULL;
            cJSON *to_child = NULL;
            // 排序对象键：保证对比顺序一致
            sort_object(from, case_sensitive);
            sort_object(to, case_sensitive);

            from_child = from->child;
            to_child = to->child;
            /* for all object values in the object with more of them */
            // 遍历所有键，处理增删改
            while ((from_child != NULL) || (to_child != NULL))
            {
                int diff;
                // 计算键的差异：确定增/删/改
                if (from_child == NULL)
                {
                    diff = 1;
                    // to有新增键
                }
                else if (to_child == NULL)
                {
                    diff = -1;// from有多余键
                }
                else
                {
                    diff = compare_strings((unsigned char*)from_child->string, (unsigned char*)to_child->string, case_sensitive);
                }

                if (diff == 0)// 键相同：递归对比值，生成replace补丁
                {
                    /* both object keys are the same */
                    size_t path_length = strlen((const char*)path);
                    size_t from_child_name_length = pointer_encoded_length((unsigned char*)from_child->string);
                    unsigned char *new_path = (unsigned char*)cJSON_malloc(path_length + from_child_name_length + sizeof("/"));

                    sprintf((char*)new_path, "%s/", path);
                    encode_string_as_pointer(new_path + path_length + 1, (unsigned char*)from_child->string);

                    /* create a patch for the element */
                    create_patches(patches, new_path, from_child, to_child, case_sensitive);
                    cJSON_free(new_path);

                    from_child = from_child->next;
                    to_child = to_child->next;
                }
                else if (diff < 0) // from的键不在to中：生成remove补丁
                {
                    /* object element doesn't exist in 'to' --> remove it */
                    compose_patch(patches, (const unsigned char*)"remove", path, (unsigned char*)from_child->string, NULL);

                    from_child = from_child->next;
                }
                else// to的键不在from中：生成add补丁
                {
                    /* object element doesn't exist in 'from' --> add it */
                    compose_patch(patches, (const unsigned char*)"add", path, (unsigned char*)to_child->string, to_child);

                    to_child = to_child->next;
                }
            }
            return;
        }

        default: // 布尔/NULL类型：类型相同则无需补丁
            break;
    }
}
/* 
 * 公开API：生成JSON Patch（大小写不敏感）
 * 设计意图：创建补丁数组，调用create_patches，根路径为空字符串
 */
CJSON_PUBLIC(cJSON *) cJSONUtils_GeneratePatches(cJSON * const from, cJSON * const to)
{
    cJSON *patches = NULL;

    if ((from == NULL) || (to == NULL))
    {
        return NULL;
    }

    patches = cJSON_CreateArray();
    create_patches(patches, (const unsigned char*)"", from, to, false);

    return patches;
}
/* 
 * 公开API：生成JSON Patch（大小写敏感）
 */
CJSON_PUBLIC(cJSON *) cJSONUtils_GeneratePatchesCaseSensitive(cJSON * const from, cJSON * const to)
{
    cJSON *patches = NULL;

    if ((from == NULL) || (to == NULL))
    {
        return NULL;
    }

    patches = cJSON_CreateArray();
    create_patches(patches, (const unsigned char*)"", from, to, true);

    return patches;
}
/* 
 * 公开API：排序JSON对象的键（大小写不敏感）
 */
CJSON_PUBLIC(void) cJSONUtils_SortObject(cJSON * const object)
{
    sort_object(object, false);
}
/* 
 * 公开API：排序JSON对象的键（大小写敏感）
 */
CJSON_PUBLIC(void) cJSONUtils_SortObjectCaseSensitive(cJSON * const object)
{
    sort_object(object, true);
}
/* 
 * 递归应用JSON Merge Patch（RFC 7396）
 * 设计意图：
 * 1. Merge Patch是简化版补丁，用JSON对象表示修改：
 *    - 标量/数组：直接替换
 *    - 对象：递归合并
 *    - NULL：删除键
 * 2. 内存管理：删除原有target节点，替换为patch节点（深拷贝）
 */
static cJSON *merge_patch(cJSON *target, const cJSON * const patch, const cJSON_bool case_sensitive)
{
    cJSON *patch_child = NULL;

    if (!cJSON_IsObject(patch))
    {
        /* scalar value, array or NULL, just duplicate */
        cJSON_Delete(target);
        return cJSON_Duplicate(patch, 1);
    }

    // target非对象：创建空对象，准备合并// patch非对象（标量/数组/NULL）：直接替换target
    if (!cJSON_IsObject(target))
    {
        cJSON_Delete(target);
        target = cJSON_CreateObject();
    }

    // 遍历patch的所有键，递归合并
    patch_child = patch->child;
    while (patch_child != NULL)
    {
        if (cJSON_IsNull(patch_child))// patch值为NULL：删除target的对应键
        {
            /* NULL is the indicator to remove a value, see RFC7396 */
            if (case_sensitive)
            {
                cJSON_DeleteItemFromObjectCaseSensitive(target, patch_child->string);
            }
            else
            {
                cJSON_DeleteItemFromObject(target, patch_child->string);
            }
        }
        else// patch值非NULL：递归合并target的对应键
        {
            cJSON *replace_me = NULL;
            cJSON *replacement = NULL;
// 分离target的对应键（准备替换）
            if (case_sensitive)
            {
                replace_me = cJSON_DetachItemFromObjectCaseSensitive(target, patch_child->string);
            }
            else
            {
                replace_me = cJSON_DetachItemFromObject(target, patch_child->string);
            }
// 递归合并：replace_me为原节点，patch_child为补丁
            replacement = merge_patch(replace_me, patch_child, case_sensitive);
            if (replacement == NULL)
            {
                cJSON_Delete(target);
                return NULL;
            }
// 将合并后的节点添加到target
            cJSON_AddItemToObject(target, patch_child->string, replacement);
        }
        patch_child = patch_child->next;
    }
    return target;
}
/* 
 * 公开API：应用JSON Merge Patch（大小写不敏感）
 */
CJSON_PUBLIC(cJSON *) cJSONUtils_MergePatch(cJSON *target, const cJSON * const patch)
{
    return merge_patch(target, patch, false);
}

CJSON_PUBLIC(cJSON *) cJSONUtils_MergePatchCaseSensitive(cJSON *target, const cJSON * const patch)
{
    return merge_patch(target, patch, true);
}
/* 
 * 递归生成JSON Merge Patch（RFC 7396）
 * 设计意图：对比from和to，生成将from转为to的Merge Patch对象
 * 规则：
 * - to为NULL：返回NULL（删除所有）
 * - 非对象：直接返回to的拷贝
 * - 对象：对比键，新增/删除/修改对应键
 */
static cJSON *generate_merge_patch(cJSON * const from, cJSON * const to, const cJSON_bool case_sensitive)
{
    cJSON *from_child = NULL;
    cJSON *to_child = NULL;
    cJSON *patch = NULL;
    if (to == NULL)// to为NULL：生成NULL补丁（删除所有）
    {
        /* patch to delete everything */
        return cJSON_CreateNull();
    }
    // 非对象：直接返回to的拷贝（替换）
    if (!cJSON_IsObject(to) || !cJSON_IsObject(from))
    {
        return cJSON_Duplicate(to, 1);
    }
 // 排序对象键：保证对比顺序一致
    sort_object(from, case_sensitive);
    sort_object(to, case_sensitive);

    from_child = from->child;
    to_child = to->child;
    // 创建空补丁对象
    patch = cJSON_CreateObject();
    if (patch == NULL)
    {
        return NULL;
    }
    // 遍历所有键，生成补丁
    while (from_child || to_child)
    {
        int diff;
        // 键名对比结果：<0/0/>0 分别表示 from键 < to键 / 相等 / from键 > to键
    // 计算两个键名的字典序差异（核心：利用有序链表的特性，避免重复遍历）
        if (from_child != NULL)
        {
            if (to_child != NULL)
            {
                // 两个键都存在：直接对比字符串（因已排序，可通过字典序判断键的相对位置）
            // 注意：此处用strcmp是因为前面已通过sort_object保证键有序，且当前上下文是case_sensitive的外层逻辑
                diff = strcmp(from_child->string, to_child->string);
            }
            else
            {
                // to_child已遍历完，但from_child还有剩余 → from的键在to中不存在（需删除），标记diff=-1
                diff = -1;
            }
        }
        else
        {
            // from_child已遍历完，但to_child还有剩余 → to的键在from中不存在（需新增），标记diff=1
            diff = 1;
        }

        if (diff < 0)
        {
            /* from has a value that to doesn't have -> remove */
        // Merge Patch规则：键值为null表示删除源对象的对应键
        // 内存管理：cJSON_CreateNull()创建的null节点，所有权转移给patch对象（由patch统一释放）
            cJSON_AddItemToObject(patch, from_child->string, cJSON_CreateNull());
// 移动from指针：继续处理下一个from的键
            from_child = from_child->next;
        }
        else if (diff > 0)
        {
            /* to has a value that from doesn't have -> add to patch */
            // Merge Patch规则：直接将to的键值加入补丁（from中无此键则新增，有则替换）
        // 内存管理：cJSON_Duplicate(to_child, 1)是深拷贝（避免补丁引用原to的节点，防止原数据被修改）
        // 所有权：拷贝后的节点通过AddItemToObject转移给patch，后续由patch统一释放
            cJSON_AddItemToObject(patch, to_child->string, cJSON_Duplicate(to_child, 1));

            to_child = to_child->next;
        }
             // 情况3：diff = 0 → 键名相同，需对比值是否一致
        else
        {
            /* object key exists in both objects */
            // 对比两个键的值是否完全一致（递归对比嵌套结构，如子对象/数组）
            if (!compare_json(from_child, to_child, case_sensitive))
            {
                /* not identical --> generate a patch */
                 // 值不一致：递归生成嵌套的Merge Patch（处理子对象/数组的差异）
            // 内存管理：GenerateMergePatch返回的补丁节点，所有权转移给当前patch
                cJSON_AddItemToObject(patch, to_child->string, cJSONUtils_GenerateMergePatch(from_child, to_child));
            }
// 注意：值一致时不做任何操作（Merge Patch仅记录差异，无差异则不生成补丁）

        /* next key in the object */
        // 两个指针同时后移：继续对比下一组键
            /* next key in the object */
            from_child = from_child->next;
            to_child = to_child->next;
        }
    }
    if (patch->child == NULL)
    {
        /* no patch generated */
        cJSON_Delete(patch);
        return NULL;
    }

    return patch;
}
// 公开API：生成大小写不敏感的Merge Patch（封装核心函数，默认case_sensitive=false）
// 设计意图：对外提供简洁接口，隐藏内部递归和排序的细节
CJSON_PUBLIC(cJSON *) cJSONUtils_GenerateMergePatch(cJSON * const from, cJSON * const to)
{
    return generate_merge_patch(from, to, false);
}

// 公开API：生成大小写敏感的Merge Patch（满足严格键名匹配场景）
// 设计意图：区分大小写场景（如JSON对象键名严格区分Name/name）
CJSON_PUBLIC(cJSON *) cJSONUtils_GenerateMergePatchCaseSensitive(cJSON * const from, cJSON * const to)
{
    return generate_merge_patch(from, to, true);
}
