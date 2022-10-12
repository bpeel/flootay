#!/usr/bin/python3

# Flootay – a video overlay generator
# Copyright (C) 2022  Neil Roberts
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import re
import sys

def remove_leading_zeroes(num):
    start = 0
    while start < len(num) - 1 and num[start] == "0":
        start += 1
    return num[start:]

def convert(e):
    def replace(md):
        parts = ['(']
        segments = md.group(1).split(':')
        segments.pop()

        for i, segment in enumerate(segments):
            parts.append("{}*{}+".format(remove_leading_zeroes(segment),
                                         60 ** (len(segments) - i)))

        parts.append(remove_leading_zeroes(md.group(2)))

        parts.append(')')

        return "".join(parts)

    return re.sub(r'((?:[0-9]+:)+)([0-9]+(?:\.[0-9]+)?)', replace, e)

def display(num):
    parts = []

    if num < 0:
        parts.append('-')
        num = -num

    comp = 60
    factor = 0

    while num >= comp:
        comp *= 60
        factor += 1

    for i in range(factor):
        comp //= 60
        parts.append("{:02d}".format(int(num / comp)))
        parts.append(':')
        num %= comp

    int_part = int(num)
    parts.append("{:02d}".format(int_part))

    num -= int_part

    if num != 0:
        parts.append(str(num)[1:])

    return "".join(parts)

if __name__ == '__main__':
    expr = convert(" ".join(sys.argv[1:]))
    print(expr)
    result = eval(expr)
    print(result)
    print(display(result))
