//===-- main.c --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

int main (int argc, char const *argv[])
{
    enum days {
        Monday = 10,
        Tuesday,
        Wednesday,
        Thursday,
        Friday,
        Saturday,
        Sunday,
        kNumDays
    };
    enum days day;
    int res = 0;
    for (day = Monday - 1; day <= kNumDays + 1; day++)
    {
        res += (int)day;
    }
    return res; // Set break point at this line.
}
