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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

/* Used by some code below as an example datatype. */
/* 
 * 自定义record结构体：模拟真实业务中的地址/地理信息数据
 * 设计思路：用const char*存储字符串（避免意外修改常量）、double存储经纬度（保证坐标精度）
 * 后续会将该结构体数组序列化为JSON对象数组，体现"结构化数据→JSON"的典型场景
 */
struct record
{
    const char *precision;// 精度类型（示例中固定为"zip"），const修饰避免字符串常量被修改
    double lat;// 纬度：用double而非float，满足地理坐标的高精度需求
    double lon;// 经度：支持科学计数法（如-1.223959e+2），测试cJSON浮点处理
    const char *address;// 详细地址（示例中为空），预留业务扩展字段
    const char *city;// 城市名称，字符串类型符合地址数据的文本特性
    const char *state;// 州/省份，北美地址体系的标准字段
    const char *zip;// 邮政编码，字符串类型兼容非数字邮编场景
    const char *country;// 国家，固定为US，模拟地域数据
};
/* 
 * 核心功能：演示cJSON_PrintPreallocated（预分配缓冲区打印）的使用
 * 对比cJSON_Print（自动分配内存）和手动预分配内存的差异，验证内存不足时的错误处理
 * 设计目的：展示cJSON的两种打印模式，以及工业级代码的内存边界检查思路
 * 返回值：0=成功，-1=失败（异常场景）
 */

/* Create a bunch of objects as demonstration. */
static int print_preallocated(cJSON *root)
{
     /* 变量设计逻辑：
     * out：cJSON_Print自动分配的JSON字符串（需手动free）
     * buf：足够大的预分配缓冲区（成功场景），+5字节冗余避免边界问题
     * buf_fail：刚好不足的缓冲区（失败场景），验证错误处理
     * len/len_fail：对应缓冲区长度，size_t适配不同平台的内存长度类型
     */
    /* declarations */
    char *out = NULL;
    char *buf = NULL;
    char *buf_fail = NULL;
    size_t len = 0;
    size_t len_fail = 0;
// cJSON_Print：自动malloc内存生成格式化JSON字符串（带缩进/换行）
    // 优点：使用简单；缺点：内存分配不可控，需手动释放
    /* formatted print */
    out = cJSON_Print(root);
    // 分配"足够大"的缓冲区：strlen(out)是JSON字符串长度，+5是经验值冗余
    // 避免因字符串终止符(\0)、格式字符等导致缓冲区刚好不足
    /* create buffer to succeed */
    /* the extra 5 bytes are because of inaccuracies when reserving memory */
    len = strlen(out) + 5;
    buf = (char*)malloc(len);// 手动分配内存，必须在函数退出前free，否则内存泄漏
    if (buf == NULL)// 内存分配失败的防御性检查：C语言内存操作的必要步骤
    {
        printf("Failed to allocate memory.\n");
        exit(1);// 内存分配失败属于致命错误，直接退出（实际项目可返回错误码）
    }

    /* create buffer to fail */
    // 分配"刚好不足"的缓冲区：仅等于JSON字符串长度，无冗余，用于触发打印失败
    len_fail = strlen(out);
    buf_fail = (char*)malloc(len_fail);
    if (buf_fail == NULL)
    {
        printf("Failed to allocate memory.\n");
        exit(1);
    }
    /* 测试预分配缓冲区打印（成功场景）
     * cJSON_PrintPreallocated参数说明：
     * root：待打印的JSON根节点；buf：预分配缓冲区；(int)len：缓冲区长度（API要求int）；1：格式化输出
     * 返回值：0=失败，非0=成功；这里判断!0即失败，进入异常处理
     */
    /* Print to buffer */
    if (!cJSON_PrintPreallocated(root, buf, (int)len, 1)) {
        printf("cJSON_PrintPreallocated failed!\n");
        // 额外验证：自动分配和预分配的输出是否一致，排查打印逻辑错误
        if (strcmp(out, buf) != 0) {
            printf("cJSON_PrintPreallocated not the same as cJSON_Print!\n");
            printf("cJSON_Print result:\n%s\n", out);
            printf("cJSON_PrintPreallocated result:\n%s\n", buf);
        }
        // 内存清理：所有malloc的内存必须释放，按分配顺序反向释放（无强制要求，但养成习惯）
        free(out);
        free(buf_fail);
        free(buf);
        return -1;
    }
// 成功场景：打印预分配缓冲区中的JSON字符串
    /* success */
    printf("%s\n", buf);

    /* force it to fail */
    /* 测试预分配缓冲区打印（失败场景）
     * 预期：缓冲区不足时返回0（失败）；若返回非0，说明API错误处理逻辑异常
     */
    if (cJSON_PrintPreallocated(root, buf_fail, (int)len_fail, 1)) {
        printf("cJSON_PrintPreallocated failed to show error with insufficient memory!\n");
        printf("cJSON_Print result:\n%s\n", out);
        printf("cJSON_PrintPreallocated result:\n%s\n", buf_fail);
        free(out);
        free(buf_fail);
        free(buf);
        return -1;
    }

    free(out);
    free(buf_fail);
    free(buf);
    return 0;
}
/* 
 * 核心功能：演示多种JSON结构的创建（对象、数组、嵌套对象、基本类型）
 * 覆盖cJSON高频API：CreateObject/CreateArray/CreateString/CreateNumber等
 * 设计思路：从简单到复杂，覆盖真实场景中常见的JSON结构（字符串数组、二维数组、嵌套对象、对象数组）
 */
/* Create a bunch of objects as demonstration. */
static void create_objects(void)
{
    /* 变量设计：
     * root：每个JSON结构的根节点（每次创建新结构前重置为NULL，避免野指针）
     * fmt/img/thm/fld：嵌套子对象指针，简化嵌套结构的操作（无需重复查找节点）
     * i：循环计数器，用于遍历数组/矩阵/结构体数组
     */
    /* declare a few. */
    cJSON *root = NULL;
    cJSON *fmt = NULL;
    cJSON *img = NULL;
    cJSON *thm = NULL;
    cJSON *fld = NULL;
    int i = 0;
// 星期字符串数组：用于创建JSON字符串数组，模拟"枚举型"文本数据
    /* Our "days of the week" array: */
    const char *strings[7] =
    {
        "Sunday",
        "Monday",
        "Tuesday",
        "Wednesday",
        "Thursday",
        "Friday",
        "Saturday"
    };
    /* Our matrix: */
    // 3x3矩阵：用于创建JSON二维数组，模拟数值型矩阵数据（如变换矩阵）
    int numbers[3][3] =
    {
        {0, -1, 0},
        {1, 0, 0},
        {0 ,0, 1}
    };
    // 图片ID数组：用于创建JSON整数数组，模拟业务中的ID列表
    /* Our "gallery" item: */
    int ids[4] = { 116, 943, 234, 38793 };
        // 地址记录数组：模拟两条真实地址数据，用于创建JSON对象数组
    // 经纬度使用科学计数法，测试cJSON对特殊浮点格式的序列化能力
    /* Our array of "records": */
    struct record fields[2] =
    {
        {
            "zip",
            37.7668,
            -1.223959e+2,
            "",
            "SAN FRANCISCO",
            "CA",
            "94107",
            "US"
        },
        {
            "zip",
            37.371991,
            -1.22026e+2,
            "",
            "SUNNYVALE",
            "CA",
            "94085",
            "US"
        }
    };
    // volatile修饰zero：避免编译器优化掉"1.0/zero"除零操作，测试无穷大数值的JSON序列化
    volatile double zero = 0.0;

    /* Here we construct some JSON standards, from the JSON site. */

    /* Our "Video" datatype: */
       /* 示例1：创建嵌套JSON对象（Video信息）
     * 演示：根对象 + 子对象 + 多类型键值对（字符串、数字、布尔）
     */
    root = cJSON_CreateObject();
    // 创建空JSON对象（{}）作为根节点，内存由cJSON管理
    // 向根对象添加"name"键：值为带转义双引号的字符串，cJSON_CreateString自动处理引号转义
    cJSON_AddItemToObject(root, "name", cJSON_CreateString("Jack (\"Bee\") Nimble"));
    // 向根对象添加"format"键：值为新创建的空对象，并将子对象指针赋值给fmt（简化后续操作）
    cJSON_AddItemToObject(root, "format", fmt = cJSON_CreateObject());
    cJSON_AddStringToObject(fmt, "type", "rect");// 子对象添加字符串键值对
    cJSON_AddNumberToObject(fmt, "width", 1920);// 子对象添加整数键值对
    cJSON_AddNumberToObject(fmt, "height", 1080);// 子对象添加整数键值对
    cJSON_AddFalseToObject (fmt, "interlace");// 子对象添加布尔键值对（false）
    cJSON_AddNumberToObject(fmt, "frame rate", 24); // 子对象添加数值键值对（支持空格键名）
// 打印验证：失败则清理root内存并退出（cJSON_Delete递归释放所有子节点，无需逐个释放fmt）
    /* Print to text */
    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);// 释放当前根节点内存，为下一个JSON结构做准备
    /* 示例2：创建JSON字符串数组（星期几）
     * 演示：cJSON_CreateStringArray快速创建字符串数组，替代循环添加，提升效率
     */
    /* Our "days of the week" array: */
    root = cJSON_CreateStringArray(strings, 7);// 直接从字符串数组创建JSON字符串数组

    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);
        /* 示例3：创建JSON二维数组（3x3矩阵）
     * 演示：根数组 + 循环添加子数组，模拟多维数值数组
     */
    /* Our matrix: */
    root = cJSON_CreateArray();// 创建空JSON数组（[]）作为根节点
    for (i = 0; i < 3; i++)
    {
        // 循环创建整数子数组，添加到根数组中；cJSON_CreateIntArray快速创建整数数组
        cJSON_AddItemToArray(root, cJSON_CreateIntArray(numbers[i], 3));
    }
// 注释示例：演示数组元素替换API（将索引1的子数组替换为字符串）
    /* cJSON_ReplaceItemInArray(root, 1, cJSON_CreateString("Replacement")); */

    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);
    /* 示例4：创建嵌套JSON对象（Gallery/Image）
     * 演示：多层嵌套对象 + 混合类型键值对（数字、字符串、数组）
     */
    /* Our "gallery" item: */
    root = cJSON_CreateObject();
    // 根对象添加"Image"键，值为新对象，赋值给img简化子对象操作
    cJSON_AddItemToObject(root, "Image", img = cJSON_CreateObject());
    cJSON_AddNumberToObject(img, "Width", 800);
    cJSON_AddNumberToObject(img, "Height", 600);
    cJSON_AddStringToObject(img, "Title", "View from 15th Floor");
    // 子对象添加"Thumbnail"键，值为新对象，赋值给thm
    cJSON_AddItemToObject(img, "Thumbnail", thm = cJSON_CreateObject());
    cJSON_AddStringToObject(thm, "Url", "http:/*www.example.com/image/481989943");
    cJSON_AddNumberToObject(thm, "Height", 125);
    cJSON_AddStringToObject(thm, "Width", "100");
    // 向Image对象添加整数数组键值对（IDs）
    cJSON_AddItemToObject(img, "IDs", cJSON_CreateIntArray(ids, 4));

    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);
    /* 示例5：创建JSON对象数组（地址记录）
     * 演示：结构体数组 → JSON对象数组，真实业务中最常见的序列化场景
     */
    /* Our array of "records": */
    root = cJSON_CreateArray(); // 根节点为数组，存储多个地址对象
    for (i = 0; i < 2; i++)
    {
        // 循环创建地址对象，添加到根数组中，赋值给fld简化字段添加
        cJSON_AddItemToArray(root, fld = cJSON_CreateObject());
        cJSON_AddStringToObject(fld, "precision", fields[i].precision);
        cJSON_AddNumberToObject(fld, "Latitude", fields[i].lat);
        cJSON_AddNumberToObject(fld, "Longitude", fields[i].lon);
        cJSON_AddStringToObject(fld, "Address", fields[i].address);
        cJSON_AddStringToObject(fld, "City", fields[i].city);
        cJSON_AddStringToObject(fld, "State", fields[i].state);
        cJSON_AddStringToObject(fld, "Zip", fields[i].zip);
        cJSON_AddStringToObject(fld, "Country", fields[i].country);
    }
 // 注释示例：演示对象字段替换API（将第二个地址的City替换为整数数组）
    /* cJSON_ReplaceItemInObject(cJSON_GetArrayItem(root, 1), "City", cJSON_CreateIntArray(ids, 4)); */

    /* cJSON_ReplaceItemInObject(cJSON_GetArrayItem(root, 1), "City", cJSON_CreateIntArray(ids, 4)); */

    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);
/* 示例6：测试非规范数值（无穷大）的序列化
     * 演示：cJSON对除零产生的Infinity的处理，验证数值兼容性
     */
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "number", 1.0 / zero);// 1.0/0.0 = Infinity，测试特殊数值

    if (print_preallocated(root) != 0) {
        cJSON_Delete(root);
        exit(EXIT_FAILURE);
    }
    cJSON_Delete(root);
}
/* 
 * 程序入口函数
 * CJSON_CDECL：cJSON定义的调用约定宏，保证跨平台兼容性（如Windows的__cdecl）
 * 核心逻辑：打印版本 + 执行JSON创建/打印演示
 */
int CJSON_CDECL main(void)
{
    // 打印cJSON版本：调试时快速确认版本，排查版本兼容问题（不同版本API可能有差异）
    /* print the version */
    printf("Version: %s\n", cJSON_Version());
    // 执行核心演示逻辑：创建各类JSON结构并打印验证
    /* Now some samplecode for building objects concisely: */
    create_objects();

    return 0;
}
