//===-- main.c --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

char my_global_char = 'X';
const char* my_global_str = "abc";
const char **my_global_str_ptr = &my_global_str;
static int my_static_int = 228;

int main (int argc, char const *argv[]) {
    return (int)my_global_char + (int)*my_global_str + argc + my_static_int;
}
