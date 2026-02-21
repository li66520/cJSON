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

#ifndef cJSON__h
#define cJSON__h
//头文件保护宏：防止重复包含（多次#include会导致结构体重复定义，编译报错）；
#ifdef __cplusplus
extern "C"
{
#endif
//extern "C"强制C++按C的规则编译该段代码，保证C编译的cJSON库能被C++项目调用
#if !defined(__WINDOWS__) && (defined(WIN32) || defined(WIN64) || defined(_MSC_VER) || defined(_WIN32))
#define __WINDOWS__
#endif
// Windows不同编译环境（MSVC/MinGW）会定义不同的宏（WIN32/_WIN32/_MSC_VER），这里统一映射为__WINDOWS__，
// 后续只需判断__WINDOWS__即可区分Windows和*nix平台，简化跨平台逻辑。
#ifdef __WINDOWS__

/* When compiling for windows, we specify a specific calling convention to avoid issues where we are being called from a project with a different default calling convention.  For windows you have 3 define options:

CJSON_HIDE_SYMBOLS - Define this in the case where you don't want to ever dllexport symbols
CJSON_EXPORT_SYMBOLS - Define this on library build when you want to dllexport symbols (default)
CJSON_IMPORT_SYMBOLS - Define this if you want to dllimport symbol

For *nix builds that support visibility attribute, you can define similar behavior by

setting default visibility to hidden by adding
-fvisibility=hidden (for gcc)
or
-xldscope=hidden (for sun cc)
to CFLAGS

then using the CJSON_API_VISIBILITY flag to "export" the same symbols the way CJSON_EXPORT_SYMBOLS does

*/

#define CJSON_CDECL __cdecl
#define CJSON_STDCALL __stdcall
//- __cdecl：C默认调用约定，调用者清理栈，支持可变参数（如printf）；
// - __stdcall：Windows API常用约定，被调用者清理栈，效率更高；
// 封装为宏，后续接口只需用CJSON_CDECL/CJSON_STDCALL，无需直接写__cdecl/__stdcall，便于维护
/* export symbols by default, this is necessary for copy pasting the C and header file */
#if !defined(CJSON_HIDE_SYMBOLS) && !defined(CJSON_IMPORT_SYMBOLS) && !defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_EXPORT_SYMBOLS
#endif
// 【设计考量】默认导出符号：如果用户未定义任何符号控制宏，默认按「动态库导出」编译；
// 这是为了兼容「直接复制cJSON.c/cJSON.h到项目」的场景
#if defined(CJSON_HIDE_SYMBOLS)
#define CJSON_PUBLIC(type)   type CJSON_STDCALL

#elif defined(CJSON_EXPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllexport) type CJSON_STDCALL
//编译动态库（DLL）时使用，将接口导出到DLL的导出表，供外部程序调用；
// __declspec(dllexport)是Windows特有的符号导出关键字。
#elif defined(CJSON_IMPORT_SYMBOLS)
#define CJSON_PUBLIC(type)   __declspec(dllimport) type CJSON_STDCALL
#endif
#else /* !__WINDOWS__ */
#define CJSON_CDECL
#define CJSON_STDCALL

#if (defined(__GNUC__) || defined(__SUNPRO_CC) || defined (__SUNPRO_C)) && defined(CJSON_API_VISIBILITY)
#define CJSON_PUBLIC(type)   __attribute__((visibility("default"))) type
#else
#define CJSON_PUBLIC(type) type
#endif
#endif

/* project version */
#define CJSON_VERSION_MAJOR 1
#define CJSON_VERSION_MINOR 7
#define CJSON_VERSION_PATCH 19

#include <stddef.h>

/* cJSON Types: */
#define cJSON_Invalid (0)
#define cJSON_False  (1 << 0)
#define cJSON_True   (1 << 1)
#define cJSON_NULL   (1 << 2)
#define cJSON_Number (1 << 3)
#define cJSON_String (1 << 4)
#define cJSON_Array  (1 << 5)
#define cJSON_Object (1 << 6)
#define cJSON_Raw    (1 << 7) /* raw json */
// 1. 每个类型占1个比特位，可通过位运算组合（如type & cJSON_String判断是否为字符串类型）；
// 2. 取值为2的幂次（1<<n），保证类型之间无重叠，位运算判断高效；
// 3. cJSON_Raw用于存储原始JSON字符串（无需解析），满足特殊场景（如嵌入其他JSON片段）。
#define cJSON_IsReference 256
#define cJSON_StringIsConst 512

/* The cJSON structure: */
typedef struct cJSON
{
    /* next/prev allow you to walk array/object chains. Alternatively, use GetArraySize/GetArrayItem/GetObjectItem */
    struct cJSON *next;
    struct cJSON *prev;
//双向链表指针：
    // 1. 用于连接同层级节点（如Object的多个键值对、Array的多个元素）；
    // 2. 双向链表支持「正向/反向遍历」「高效删除节点」（删除时只需修改prev/next，无需遍历整个链表）；
    // 3. 相比单向链表，反向遍历（如从最后一个节点往前找）更高效，适合需要逆序处理的场景。

    /* An array or object item will have a child pointer pointing to a chain of the items in the array/object. */
    struct cJSON *child;
//子节点指针（树形结构核心）：
    // 1. 仅Object/Array节点有child（指向第一个子节点），基础类型（String/Number）无child；
    // 2. 结合next/prev，形成「树形+链表」的复合结构，完美匹配JSON的嵌套特性（Object/Array嵌套其他类型）。
    /* The type of the item, as above. */
    int type;

    /* The item's string, if type==cJSON_String  and type == cJSON_Raw */
    char *valuestring;
    /* writing to valueint is DEPRECATED, use cJSON_SetNumberValue instead */
    int valueint;
    /* The item's number, if type==cJSON_Number */
    double valuedouble;

    /* The item's name string, if this item is the child of, or is in the list of subitems of an object. */
    char *string;
} cJSON;
// 1. 复合结构：双向链表（next/prev）+ 树形结构（child），匹配JSON的层级+顺序特性；
// 2. 内存优化：字段按需复用，位掩码整合类型+状态，减少结构体大小；
// 3. 兼容性：保留废弃字段（valueint），保证旧代码可运行；
// 4. 内存安全：通过标记位（cJSON_IsReference/cJSON_StringIsConst）避免错误释放内存。

typedef struct cJSON_Hooks
{
      /* malloc/free are CDECL on Windows regardless of the default calling convention of the compiler, so ensure the hooks allow passing those functions directly. */
      void *(CJSON_CDECL *malloc_fn)(size_t sz);
      void (CJSON_CDECL *free_fn)(void *ptr);
} cJSON_Hooks;
// 内存钩子结构体：
// 1. 允许用户替换默认的malloc/free（如替换为内存池、嵌入式系统的静态内存分配器）；
// 2. 强制指定CJSON_CDECL调用约定（Windows下malloc/free是CDECL），避免调用约定不兼容导致崩溃；
// 3. 应用场景：嵌入式系统无标准malloc/free，或高频创建/释放节点时用内存池提升性能。

typedef int cJSON_bool;

/* Limits how deeply nested arrays/objects can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_NESTING_LIMIT
#define CJSON_NESTING_LIMIT 1000
#endif
// 1. cJSON解析JSON用递归下降算法，嵌套过深（如{...{...}...}嵌套10000层）会导致栈溢出；
// 2. 默认限制1000层，兼顾大部分场景（正常JSON嵌套不会超过100层）和栈安全；
// 3. 允许用户通过#define CJSON_NESTING_LIMIT修改，适配特殊场景（如深度嵌套的JSON）。

/* Limits the length of circular references can be before cJSON rejects to parse them.
 * This is to prevent stack overflows. */
#ifndef CJSON_CIRCULAR_LIMIT
#define CJSON_CIRCULAR_LIMIT 10000
#endif

/* returns the version of cJSON as a string */
CJSON_PUBLIC(const char*) cJSON_Version(void);

/* Supply malloc, realloc and free functions to cJSON */
CJSON_PUBLIC(void) cJSON_InitHooks(cJSON_Hooks* hooks);

/* Memory Management: the caller is always responsible to free the results from all variants of cJSON_Parse (with cJSON_Delete) and cJSON_Print (with stdlib free, cJSON_Hooks.free_fn, or cJSON_free as appropriate). The exception is cJSON_PrintPreallocated, where the caller has full responsibility of the buffer. */
/* Supply a block of JSON, and this returns a cJSON object you can interrogate. */
CJSON_PUBLIC(cJSON *) cJSON_Parse(const char *value);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLength(const char *value, size_t buffer_length);
/* ParseWithOpts allows you to require (and check) that the JSON is null terminated, and to retrieve the pointer to the final byte parsed. */
/* If you supply a ptr in return_parse_end and parsing fails, then return_parse_end will contain a pointer to the error so will match cJSON_GetErrorPtr(). */
CJSON_PUBLIC(cJSON *) cJSON_ParseWithOpts(const char *value, const char **return_parse_end, cJSON_bool require_null_terminated);
CJSON_PUBLIC(cJSON *) cJSON_ParseWithLengthOpts(const char *value, size_t buffer_length, const char **return_parse_end, cJSON_bool require_null_terminated);
// 1. return_parse_end：输出参数，返回解析结束的指针（或错误位置），便于定位解析失败原因；
// 2. require_null_terminated：强制要求JSON字符串以NULL终止（安全检查，避免越界）；
// 3. 分层设计：基础接口（Parse）封装通用逻辑，Opts接口暴露高级选项，兼顾易用性和灵活性。

/* Render a cJSON entity to text for transfer/storage. */
CJSON_PUBLIC(char *) cJSON_Print(const cJSON *item);
/* Render a cJSON entity to text for transfer/storage without any formatting. */
CJSON_PUBLIC(char *) cJSON_PrintUnformatted(const cJSON *item);
// 1. 生成紧凑的JSON字符串（无空格/换行），减少传输/存储体积；
// 2. 性能比Print高（无需处理缩进），适合网络传输、文件存储等场景。
/* Render a cJSON entity to text using a buffered strategy. prebuffer is a guess at the final size. guessing well reduces reallocation. fmt=0 gives unformatted, =1 gives formatted */
CJSON_PUBLIC(char *) cJSON_PrintBuffered(const cJSON *item, int prebuffer, cJSON_bool fmt);
// 1. prebuffer：预分配缓冲区大小（猜测最终字符串长度），减少realloc次数，提升性能；
// 2. fmt：控制是否格式化（0=非格式化，1=格式化），整合Print和PrintUnformatted的功能；
// 应用场景：高频序列化场景（如服务器批量生成JSON响应），减少内存分配开销。
/* Render a cJSON entity to text using a buffer already allocated in memory with given length. Returns 1 on success and 0 on failure. */
/* NOTE: cJSON is not always 100% accurate in estimating how much memory it will use, so to be safe allocate 5 bytes more than you actually need */
CJSON_PUBLIC(cJSON_bool) cJSON_PrintPreallocated(cJSON *item, char *buffer, const int length, const cJSON_bool format);
// 1. 用户提供缓冲区，cJSON直接写入，无内存分配（性能最优）；
// 2. 返回布尔值表示成功/失败，避免返回字符串的内存管理；
// 3. 注意事项：缓冲区需预留5字节冗余（cJSON估算长度可能有误差），防止缓冲区溢出；
// 应用场景：嵌入式系统（内存受限）、实时系统（要求无动态内存分配）。
/* Delete a cJSON entity and all subentities. */
CJSON_PUBLIC(void) cJSON_Delete(cJSON *item);

/* Returns the number of items in an array (or object). */
CJSON_PUBLIC(int) cJSON_GetArraySize(const cJSON *array);
/* Retrieve item number "index" from array "array". Returns NULL if unsuccessful. */
CJSON_PUBLIC(cJSON *) cJSON_GetArrayItem(const cJSON *array, int index);
// 1. 遍历数组的child链表（从第一个子节点开始，走next指针index次）；
// 2. 时间复杂度O(n)：因为是链表结构，无法随机访问，这是链表的固有缺陷；
// 3. 防御性检查：index越界/非Array节点返回NULL，避免崩溃。
/* Get item "string" from object. Case insensitive. */
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItem(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON *) cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
CJSON_PUBLIC(cJSON_bool) cJSON_HasObjectItem(const cJSON *object, const char *string);
// 1. 封装GetObjectItem的逻辑，返回布尔值，比直接调用GetObjectItem更语义化；
// 2. 避免用户写重复的遍历代码，提升易用性。
/* For analysing failed parses. This returns a pointer to the parse error. You'll probably need to look a few chars back to make sense of it. Defined when cJSON_Parse() returns 0. 0 when cJSON_Parse() succeeds. */
CJSON_PUBLIC(const char *) cJSON_GetErrorPtr(void);

/* Check item type and return its value */
CJSON_PUBLIC(char *) cJSON_GetStringValue(const cJSON * const item);
CJSON_PUBLIC(double) cJSON_GetNumberValue(const cJSON * const item);

/* These functions check the type of an item */
CJSON_PUBLIC(cJSON_bool) cJSON_IsInvalid(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsFalse(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsTrue(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsBool(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNull(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsNumber(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsString(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsArray(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsObject(const cJSON * const item);
CJSON_PUBLIC(cJSON_bool) cJSON_IsRaw(const cJSON * const item);
// 1. 封装位运算判断逻辑（如cJSON_IsString = (item->type & cJSON_String) != 0）；
// 2. 语义化命名，比用户手动写位运算更易读、更少出错；
// 3. 空指针安全：传入NULL返回cJSON_False，避免崩溃
/* These calls create a cJSON item of the appropriate type. */
CJSON_PUBLIC(cJSON *) cJSON_CreateNull(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateTrue(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateFalse(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateBool(cJSON_bool boolean);
CJSON_PUBLIC(cJSON *) cJSON_CreateNumber(double num);
CJSON_PUBLIC(cJSON *) cJSON_CreateString(const char *string);
/* raw json */
CJSON_PUBLIC(cJSON *) cJSON_CreateRaw(const char *raw);
CJSON_PUBLIC(cJSON *) cJSON_CreateArray(void);
CJSON_PUBLIC(cJSON *) cJSON_CreateObject(void);
// 1. 每种类型对应独立接口，语义化命名，避免用户手动设置type字段出错；
// 2. 内存管理：返回堆内存节点，用户需用cJSON_Delete释放；
// 3. CreateString/CreateRaw：复制输入字符串到堆内存（除非用CreateStringReference），避免原字符串失效导致的野指针
/* Create a string where valuestring references a string so
 * it will not be freed by cJSON_Delete */
CJSON_PUBLIC(cJSON *) cJSON_CreateStringReference(const char *string);
/* Create an object/array that only references it's elements so
 * they will not be freed by cJSON_Delete */
CJSON_PUBLIC(cJSON *) cJSON_CreateObjectReference(const cJSON *child);
CJSON_PUBLIC(cJSON *) cJSON_CreateArrayReference(const cJSON *child);
// 1. 标记节点为cJSON_IsReference，cJSON_Delete不释放其内存（包括valuestring/child）；
// 2. 应用场景：复用已有的节点/字符串（如常量字符串、其他结构的节点），避免重复拷贝；
// 3. 注意事项：用户需保证引用的内存生命周期长于cJSON节点，否则会出现野指针。
/* These utilities create an Array of count items.
 * The parameter count cannot be greater than the number of elements in the number array, otherwise array access will be out of bounds.*/
CJSON_PUBLIC(cJSON *) cJSON_CreateIntArray(const int *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateFloatArray(const float *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateDoubleArray(const double *numbers, int count);
CJSON_PUBLIC(cJSON *) cJSON_CreateStringArray(const char *const *strings, int count);
// 1. 封装「创建数组+循环添加元素」的逻辑，减少用户样板代码；
// 2. 类型匹配：Int/Float/DoubleArray对应不同数值类型，StringArray对应字符串数组；
// 3. 安全提示：count不能超过数组长度，否则会数组越界（接口不做检查，需用户保证）；
// 4. 内存管理：数组节点和元素节点都是堆内存，需用cJSON_Delete释放。
/* Append item to the specified array/object. */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
/* Use this when string is definitely const (i.e. a literal, or as good as), and will definitely survive the cJSON object.
 * WARNING: When this function was used, make sure to always check that (item->type & cJSON_StringIsConst) is zero before
 * writing to `item->string` */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemToObjectCS(cJSON *object, const char *string, cJSON *item);
/* Append reference to item to the specified array/object. Use this when you want to add an existing cJSON to a new cJSON, but don't want to corrupt your existing cJSON. */
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToArray(cJSON *array, cJSON *item);
CJSON_PUBLIC(cJSON_bool) cJSON_AddItemReferenceToObject(cJSON *object, const char *string, cJSON *item);
// 1. 标记item为cJSON_IsReference，添加到数组/对象但不转移所有权；
// 2. 应用场景：复用已有节点（如多个JSON共用同一个子节点），避免重复创建；
// 3. 内存管理：cJSON_Delete父节点时不释放item，需用户手动释放原有item
/* Remove/Detach items from Arrays/Objects. */
CJSON_PUBLIC(cJSON *) cJSON_DetachItemViaPointer(cJSON *parent, cJSON * const item);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(void) cJSON_DeleteItemFromArray(cJSON *array, int which);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(cJSON *) cJSON_DetachItemFromObjectCaseSensitive(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObject(cJSON *object, const char *string);
CJSON_PUBLIC(void) cJSON_DeleteItemFromObjectCaseSensitive(cJSON *object, const char *string);
// 1. Detach：移除节点但不释放（返回节点指针，用户可复用）；
// 2. Delete：移除并释放节点（无返回值）；
// 3. ViaPointer：按指针移除（高效，无需遍历）；按索引/键名移除（易用，需遍历）；
// 4. 双向链表操作：移除时修改prev/next指针，保证链表完整性（如array->child指向新的第一个节点）。
/* Update array items. */
CJSON_PUBLIC(cJSON_bool) cJSON_InsertItemInArray(cJSON *array, int which, cJSON *newitem); /* Shifts pre-existing items to the right. */
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemViaPointer(cJSON * const parent, cJSON * const item, cJSON * replacement);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInArray(cJSON *array, int which, cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObject(cJSON *object,const char *string,cJSON *newitem);
CJSON_PUBLIC(cJSON_bool) cJSON_ReplaceItemInObjectCaseSensitive(cJSON *object,const char *string,cJSON *newitem);
// 1. InsertItemInArray：插入节点到指定位置（后续节点右移），突破尾插法的限制；
// 2. ReplaceItem：替换节点（先删除旧节点，再插入新节点），自动释放旧节点内存；
// 3. ViaPointer：按指针替换（高效），按索引/键名替换（易用）；
// 4. 内存管理：替换时自动释放旧节点，避免内存泄漏。
/* Duplicate a cJSON item */
CJSON_PUBLIC(cJSON *) cJSON_Duplicate(const cJSON *item, cJSON_bool recurse);
/* Duplicate will create a new, identical cJSON item to the one you pass, in new memory that will
 * need to be released. With recurse!=0, it will duplicate any children connected to the item.
 * The item->next and ->prev pointers are always zero on return from Duplicate. */
/* Recursively compare two cJSON items for equality. If either a or b is NULL or invalid, they will be considered unequal.
 * case_sensitive determines if object keys are treated case sensitive (1) or case insensitive (0) */
// 1. recurse：是否递归拷贝子节点（1=递归，0=仅拷贝当前节点）；
// 2. 内存管理：返回新的堆内存节点，需用cJSON_Delete释放；
// 3. 注意事项：next/prev指针置NULL（拷贝后的节点是独立的，不继承原链表关系）；
// 应用场景：复制节点到另一个JSON结构，避免修改原节点
CJSON_PUBLIC(cJSON_bool) cJSON_Compare(const cJSON * const a, const cJSON * const b, const cJSON_bool case_sensitive);

/* Minify a strings, remove blank characters(such as ' ', '\t', '\r', '\n') from strings.
 * The input pointer json cannot point to a read-only address area, such as a string constant, 
 * but should point to a readable and writable address area. */
CJSON_PUBLIC(void) cJSON_Minify(char *json);

/* Helper functions for creating and adding items to an object at the same time.
 * They return the added item or NULL on failure. */
CJSON_PUBLIC(cJSON*) cJSON_AddNullToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddTrueToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddFalseToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddBoolToObject(cJSON * const object, const char * const name, const cJSON_bool boolean);
CJSON_PUBLIC(cJSON*) cJSON_AddNumberToObject(cJSON * const object, const char * const name, const double number);
CJSON_PUBLIC(cJSON*) cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
CJSON_PUBLIC(cJSON*) cJSON_AddRawToObject(cJSON * const object, const char * const name, const char * const raw);
CJSON_PUBLIC(cJSON*) cJSON_AddObjectToObject(cJSON * const object, const char * const name);
CJSON_PUBLIC(cJSON*) cJSON_AddArrayToObject(cJSON * const object, const char * const name);
// 1. 封装「创建节点+添加到对象」的逻辑（如AddStringToObject = CreateString + AddItemToObject）；
// 2. 返回添加的节点指针，便于后续修改（如修改字符串值）；
// 3. 减少用户代码量，提升开发效率，降低出错概率。
/* When assigning an integer value, it needs to be propagated to valuedouble too. */
#define cJSON_SetIntValue(object, number) ((object) ? (object)->valueint = (object)->valuedouble = (number) : (number))
/* helper for the cJSON_SetNumberValue macro */
// 1. 同时设置valueint和valuedouble（保证两个字段值一致，兼容旧代码）；
// 2. 空指针安全：object为NULL时返回number，避免崩溃；
// 3. 宏定义而非函数：减少函数调用开销，提升性能。
CJSON_PUBLIC(double) cJSON_SetNumberHelper(cJSON *object, double number);
#define cJSON_SetNumberValue(object, number) ((object != NULL) ? cJSON_SetNumberHelper(object, (double)number) : (number))
// 1. 封装类型检查（确保object是Number类型），避免错误设置非数字节点的值；
// 2. 用helper函数实现复杂逻辑（宏无法实现），宏封装空指针检查，兼顾性能和安全性；
// 3. 返回设置的数值，便于链式调用
/* Change the valuestring of a cJSON_String object, only takes effect when type of object is cJSON_String */
CJSON_PUBLIC(char*) cJSON_SetValuestring(cJSON *object, const char *valuestring);

/* If the object is not a boolean type this does nothing and returns cJSON_Invalid else it returns the new type*/
#define cJSON_SetBoolValue(object, boolValue) ( \
    (object != NULL && ((object)->type & (cJSON_False|cJSON_True))) ? \
    (object)->type=((object)->type &(~(cJSON_False|cJSON_True)))|((boolValue)?cJSON_True:cJSON_False) : \
    cJSON_Invalid\
)
// 1. 位运算修改type字段（先清除旧布尔标记，再设置新标记）；
// 2. 类型检查：仅布尔节点有效，非布尔节点返回cJSON_Invalid；
// 3. 空指针安全：object为NULL返回cJSON_Invalid，避免崩溃；
// 4. 宏定义：无函数调用开销，高效修改布尔值。
/* Macro for iterating over an array or object */
#define cJSON_ArrayForEach(element, array) for(element = (array != NULL) ? (array)->child : NULL; element != NULL; element = element->next)
// 1. 统一数组/对象的遍历逻辑（两者都是child指向第一个子节点，next连接后续节点）；
// 2. 空指针安全：array为NULL时element置NULL，循环不执行；
// 3. 简化遍历代码：替代手动写for循环，减少样板代码，提升可读性
/* malloc/free objects using the malloc/free functions that have been set with cJSON_InitHooks */
CJSON_PUBLIC(void *) cJSON_malloc(size_t size);
CJSON_PUBLIC(void) cJSON_free(void *object);
// 1. 统一内存分配/释放入口（无论是否设置钩子，都通过该函数）；
// 2. 若用户设置了cJSON_Hooks，使用自定义的malloc/free；否则使用标准库函数；
// 3. 对外暴露该函数，允许用户手动使用cJSON的内存管理策略（如分配缓冲区给PrintPreallocated）
#ifdef __cplusplus
}
#endif

#endif
