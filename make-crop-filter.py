#!/usr/bin/python3

# Flootay – a video overlay generator
# Copyright (C) 2023  Neil Roberts
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

FRAME_RE = re.compile(r'^\s*key_frame\s+(?P<time_stamp>[0-9]+(?:\.[0-9]+?))\s*'
                      r'{\s*x1\s+(?P<x_pos>[0-9]+)')

frames = []

for line in sys.stdin:
    md = FRAME_RE.match(line)

    if md is None:
        continue

    frames.append((float(md.group('time_stamp')), int(md.group('x_pos'))))

last_time = 0
last_pos = frames[0][1]

parts = []

for time, pos in frames[:-1]:
    parts.append(f"if(lt(t,{time}),")
    if last_pos == frames[0][1]:
        parts.append(f"{last_pos}")
    else:
        parts.append(f"(t-{last_time})"
                     f"*{pos-last_pos}"
                     f"/{time-last_time}"
                     f"+{last_pos}")
    parts.append(",")

    last_time = time
    last_pos = pos

parts.append(f"{frames[-1][1]}")

parts.append(")" * (len(frames) - 1))

print("".join(parts))
