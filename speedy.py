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

import collections
import re
import sys
import subprocess
import shlex
import dateutil.parser
import os

class Video:
    def __init__(self, filename, start_time, end_time, length):
        self.filename = filename
        self.start_time = start_time
        self.end_time = end_time
        self.length = length
        self.sounds = []
        self.script = []
        self.filter = []

        self.is_image = re.search(r'\.(?:jpe?g|png)$', filename) is not None

        self.use_gpx = not filename.startswith("|") and not self.is_image

Script = collections.namedtuple('Script', ['videos',
                                           'scores',
                                           'svgs',
                                           'gpx_offsets',
                                           'speed_overrides',
                                           'show_elevation',
                                           'show_map',
                                           'sound_args'])
Svg = collections.namedtuple('Svg', ['video',
                                     'filename',
                                     'start_time',
                                     'length'])
Sound = collections.namedtuple('Sound', ['start_time', 'filename', 'length'])
SpeedOverride = collections.namedtuple('SpeedOverride',
                                       ['filename',
                                        'start_time',
                                        'length',
                                        'speed'])
# Length is the time in seconds of the source video, ie, not accelerated
VideoSpeed = collections.namedtuple('VideoSpeed', ['length', 'speed'])
# Filename can be None if silence should be played
SoundClip = collections.namedtuple('SoundClip', ['filename', 'length'])
ScoreDiff = collections.namedtuple('ScoreDiff', ['video', 'time', 'diff'])

TIME_RE = re.compile(r'(?:([0-9]+):)?([0-9]+)(\.[0-9]+)?')

SPEED_UP = 1.0 / 3.0

FPS = 30

class ParseError(Exception):
    pass

def decode_time(time_str):
    md = TIME_RE.match(time_str)
    seconds = int(md.group(2))

    if md.group(1):
        seconds += int(md.group(1)) * 60

    if md.group(3):
        seconds += float("0" + md.group(3))

    return seconds

def parse_script(infile):
    sound_re = re.compile(r'(?P<time>' +
                          TIME_RE.pattern +
                          r')' +
                          r'\s+(?P<filename>.*)')
    video_re = re.compile(r'(?P<filename>.*?)' +
                          r'(?:\s+(?P<start_time>' +
                          TIME_RE.pattern +
                          r')(?:\s+(?P<end_time>' +
                          TIME_RE.pattern +
                          r'))?)?$')
    score_re = re.compile(r'(?P<time>' +
                          TIME_RE.pattern +
                          r')\s+(?P<diff>[+-][0-9]+)\s*$')
    svg_re = re.compile(r'(?P<start_time>' +
                        TIME_RE.pattern +
                        r')\s+(?P<length>' +
                        TIME_RE.pattern +
                        r')\s+(?P<filename>.*\.svg)\s*$')
    gpx_offset_re = re.compile(r'gpx_offset\s+(?P<filename>\S+)\s+'
                               r'(?P<video_time>'
                               + TIME_RE.pattern +
                               r')\s+(?P<utc_time>.*)')
    slow_re = re.compile(r'slow\s+(?P<start_time>' +
                         TIME_RE.pattern +
                         r')\s+(?P<end_time>' +
                         TIME_RE.pattern +
                         r')$')
    speed_re = re.compile(r'(?P<speed>[0-9]+(?:\.[0-9]+)?)x\s+'
                          r'(?P<start_time>' +
                          TIME_RE.pattern +
                          r')\s+(?P<end_time>' +
                          TIME_RE.pattern +
                          r')$')
    sound_args_re = re.compile(r'sound_args\s+(?P<args>.*)')
    filter_re = re.compile(r'filter\s+(?P<filter>.*)')

    videos = []
    scores = []
    svgs = []
    speed_overrides = []
    sound_args = []
    gpx_offsets = {}
    show_elevation = False
    show_map = False

    in_script = False
    
    for line_num, line in enumerate(infile):
        line = line.strip()

        if in_script:
            if line == "}}":
                in_script = False
            else:
                videos[-1].script.append(line)
            continue

        if line == "{{":
            in_script = True
            continue

        if len(line) <= 0 or line[0] == '#':
            continue

        if line == "logo":
            video_filename = ("|" +
                              os.path.join(os.path.dirname(sys.argv[0]),
                                           "build",
                                           "generate-logo"))
            video = Video(video_filename, 0, 3, 3)
            videos.append(video)
            sound_filename = os.path.join(os.path.dirname(sys.argv[0]),
                                          "logo-sound.flac")
            video.sounds.append(Sound(0,
                                      sound_filename,
                                      get_video_length(sound_filename)))
            speed_overrides.append(SpeedOverride(video_filename,
                                                 0, # start_time
                                                 3, # end_time
                                                 1.0))
            continue

        if line == "elevation":
            show_elevation = True
            continue

        if line == "map":
            show_map = True
            continue

        if line == "no_gpx":
            videos[-1].use_gpx = False
            continue

        md = filter_re.match(line)
        if md:
            videos[-1].filter.append(md.group('filter'))
            continue

        md = sound_args_re.match(line)
        if md:
            sound_args.extend(shlex.split(md.group('args')))
            continue

        md = slow_re.match(line)
        if md:
            start_time = decode_time(md.group('start_time'))
            end_time = decode_time(md.group('end_time'))
            speed_overrides.append(SpeedOverride(videos[-1].filename,
                                                 start_time,
                                                 end_time - start_time,
                                                 1.0))
            continue

        md = speed_re.match(line)
        if md:
            speed = 1.0 / float(md.group('speed'))
            start_time = decode_time(md.group('start_time'))
            end_time = decode_time(md.group('end_time'))
            speed_overrides.append(SpeedOverride(videos[-1].filename,
                                                 start_time,
                                                 end_time - start_time,
                                                 speed))
            continue

        md = gpx_offset_re.match(line)
        if md:
            timestamp = dateutil.parser.parse(md.group('utc_time'))
            offset = (timestamp.timestamp() -
                      decode_time(md.group('video_time')))
            gpx_offsets[md.group('filename')] = offset
            continue

        md = svg_re.match(line)
        if md:
            if len(videos) <= 0:
                raise ParseError(("line {}: svg specified "
                                  "with no video").format(line_num + 1))

            svgs.append(Svg(videos[-1],
                            md.group('filename'),
                            decode_time(md.group('start_time')),
                            decode_time(md.group('length'))))

            continue
 
        md = score_re.match(line)
        if md:
            if len(videos) <= 0:
                raise ParseError(("line {}: score specified "
                                  "with no video").format(line_num + 1))

            scores.append(ScoreDiff(videos[-1],
                                    decode_time(md.group('time')),
                                    int(md.group('diff'))))
            continue

        md = sound_re.match(line)
        if md:
            if len(videos) <= 0:
                raise ParseError(("line {}: sound specified "
                                  "with no video").format(line_num + 1))

            start_time = decode_time(md.group('time'))
            filename = md.group('filename')
            length = get_video_length(filename)

            sound = Sound(start_time, filename, length)
            videos[-1].sounds.append(sound)

            continue

        md = video_re.match(line)

        filename = md.group('filename')

        start_time = md.group('start_time')
        if start_time:
            start_time = decode_time(start_time)
        else:
            start_time = 0

        end_time = md.group('end_time')
        if end_time:
            end_time = decode_time(end_time)

        if filename.startswith('|'):
            video_length = end_time - start_time
        else:
            video_length = get_video_length(filename)

        videos.append(Video(filename,
                            start_time,
                            end_time,
                            video_length))

    return Script(videos,
                  scores,
                  svgs,
                  gpx_offsets,
                  speed_overrides,
                  show_elevation,
                  show_map,
                  sound_args)

def get_video_length(filename):
    s = subprocess.check_output(["ffprobe",
                                 "-i", filename,
                                 "-show_entries", "format=duration",
                                 "-v", "quiet",
                                 "-of", "csv=p=0"])
    return float(s)

def get_videos_length(videos):
    total_length = 0

    for video in videos:
        if video.end_time is None:
            length = video.length - video.start_time
        else:
            length = video.end_time - video.start_time

        total_length += length

    return total_length

def get_input_time(videos, filename, t):
    total_time = 0

    for video in videos:
        if video.end_time is None:
            end_time = video.length
        else:
            end_time = video.end_time

        if (filename == video.filename and
            t >= video.start_time and
            t < end_time):
            return total_time + t - video.start_time

        total_time += end_time - video.start_time

    raise Exception("Couldn’t find input time in {} at {}".format(filename, t))

def get_output_time(videos, video_speeds, filename, time):
    input_time = get_input_time(videos, filename, time)
    total_input_time = 0
    total_output_time = 0

    for vs in video_speeds:
        if input_time < total_input_time + vs.length:
            return (total_output_time +
                    (input_time - total_input_time) *
                    vs.speed)

        total_input_time += vs.length
        total_output_time += vs.length * vs.speed

    raise Exception("Couldn’t find output time in {} at {}".format(filename,
                                                                   time))

def get_video_speeds(videos, speed_overrides):
    total_input_length = get_videos_length(videos)

    times = [[get_input_time(videos, st.filename, st.start_time),
              st.length,
              st.speed]
             for st in speed_overrides]
    times.sort()

    last_time = 0
    video_speeds = []

    for t in times:
        if last_time > 0 and t[0] <= last_time:
            last_speed = video_speeds[-1]

            if t[0] + t[1] > last_time:
                if t[2] == video_speeds[-1].speed:
                    video_speeds[-1] = VideoSpeed(last_speed.length +
                                                  t[0] +
                                                  t[1] -
                                                  last_time,
                                                  last_speed.speed)
                else:
                    video_speeds.append(VideoSpeed(t[0] + t[1] - last_time,
                                                   t[2]))
                last_time = t[0] + t[1]
        else:
            if t[0] > last_time:
                video_speeds.append(VideoSpeed(t[0] - last_time, SPEED_UP))

            video_speeds.append(VideoSpeed(t[1], t[2]))
            last_time = t[0] + t[1]

    if last_time < total_input_length:
        video_speeds.append(VideoSpeed(total_input_length - last_time,
                                       SPEED_UP))

    return video_speeds

def get_sound_clips(videos, video_speeds):
    sound_clips = []
    sound_pos = 0

    for video in videos:
        for sound in video.sounds:
            sound_clip_pos = get_output_time(videos,
                                             video_speeds,
                                             video.filename,
                                             sound.start_time)

            if sound_clip_pos < sound_pos:
                raise Exception(("Sound {} overlaps previous sound by {} "
                                 "seconds").
                                format(sound.filename,
                                       sound_pos - sound_clip_pos))

            if sound_clip_pos > sound_pos:
                sound_clips.append(SoundClip(None, sound_clip_pos - sound_pos))
                
            sound_clips.append(SoundClip(sound.filename, sound.length))
            sound_pos = sound_clip_pos + sound.length

    return sound_clips

def get_ffmpeg_input_args(video):
    args = []

    if video.start_time > 0:
        args.extend(["-ss", str(video.start_time)])

    if video.end_time is not None:
        args.extend(["-to", str(video.end_time)])

    if video.filename.startswith("|"):
        args.extend(["-f",
                     "rawvideo",
                     "-pixel_format",
                     "rgb32",
                     "-video_size",
                     "1920x1080",
                     "-framerate",
                     "30"])
    elif video.is_image:
        args.extend(["-framerate", "30", "-loop", "1"])

        if video.end_time is None:
            raise "Missing end time on infinite image input"

    args.extend(["-i", video.filename])

    return args

def get_ffmpeg_filter(videos, video_speeds):
    input_time = 0
    output_time = 0

    parts = []

    for i, video in enumerate(videos):
        if not video.filename.startswith("|"):
            parts.append("[{}]".format(i))

            if len(video.filter) > 0:
                parts.append(",".join(video.filter) + ",")

            parts.append("scale=1920:1080[sv{}];".format(i))

    for i, video in enumerate(videos):
        parts.append("[")
        if not video.filename.startswith("|"):
            parts.append("sv")
        parts.append("{}]".format(i))

    parts.extend(["concat=n={}:v=1:a=0[ccv];".format(len(videos)),
                  "[ccv]setpts='"])

    for i, vs in enumerate(video_speeds):
        if i < len(video_speeds) - 1:
            parts.append("if(lt(T-STARTT,{}),".format(input_time + vs.length))

        parts.append("STARTPTS+{}/TB+(PTS-STARTPTS-{}/TB)".format(output_time,
                                                                  input_time))
        if vs.speed != 1.0:
            parts.append("*{}".format(vs.speed))

        if i < len(video_speeds) - 1:
            parts.append(",")

        input_time += vs.length
        output_time += vs.length * vs.speed

    parts.append(")" * (len(video_speeds) - 1))
    parts.append("',trim=duration={}[outv]".format(output_time))

    return "".join(parts)

def get_ffmpeg_args(videos, video_speeds):
    input_args = sum((get_ffmpeg_input_args(video) for video in videos), [])

    next_input = len(videos)

    flootay_input = next_input
    next_input += 1
    input_args.extend(["-f", "rawvideo",
                       "-pixel_format", "rgba",
                       "-video_size", "1920x1080",
                       "-framerate", "30",
                       "-i", "|./scores.flt"])

    sound_input = next_input
    next_input += 1
    input_args.extend(["-ar", "48000",
                       "-ac", "2",
                       "-f", "s24le",
                       "-c:a", "pcm_s24le",
                       "-i", "|./sound.sh"])

    filter = (get_ffmpeg_filter(videos, video_speeds) + ";" +
              "[outv][{}]overlay[overoutv]".format(flootay_input))

    return input_args + ["-filter_complex", filter,
                         "-map", "[overoutv]",
                         "-map", "{}:a".format(sound_input)]

def write_sound_script(f, total_video_time, sound_clips):
    dirname = os.path.dirname(sys.argv[0])
    if len(dirname) == 0:
        dirname = "."
    exe = os.path.join(dirname, "build", "generate-sound")

    sound_args = " ".join(shlex.quote(a) for a in script.sound_args)

    print(("#!/bin/bash\n"
           "\n"
           "exec {} -E {} {}").format(exe, total_video_time, sound_args),
          end='',
          file=f)

    pos = 0

    for clip in sound_clips:
        if clip.filename:
            print(" -s {} {}".format(pos, shlex.quote(clip.filename)),
                  end='',
                  file=f)
        pos += clip.length

    print("", file=f)

def write_score_script(f, scores, videos, video_speeds):
    if len(scores) <= 0:
        return

    print("score {", file=f)

    value = 0

    for score in scores:
        value += score.diff
        time = get_output_time(videos,
                               video_speeds,
                               score.video.filename,
                               score.time)
        print("        key_frame {} {{ v {} }}".format(round(time * FPS),
                                                       value),
              file=f)

    end_time = sum(vs.length * vs.speed for vs in video_speeds)

    print("        key_frame {} {{ v {} }}\n".format(round(end_time * FPS),
                                                     value) +
          "}\n",
          file=f)

def write_svg_script(f, svgs, videos, video_speeds):
    for svg in svgs:
        start_time = get_output_time(videos,
                                     video_speeds,
                                     svg.video.filename,
                                     svg.start_time)

        print(("svg {{\n"
               "        file \"{}\"\n"
               "        key_frame {} {{ }}\n"
               "        key_frame {} {{ }}\n"
               "}}\n").format(svg.filename,
                              round(start_time * FPS),
                              round((start_time + svg.length) * FPS)),
              file=f)

def get_video_gpx_offsets(script):
    raw_footage = dict((os.path.basename(video.filename),
                        video.length)
                       for video in script.videos
                       if video.use_gpx)

    last_offset = None
    offsets = {}

    for filename in sorted(raw_footage.keys()):
        if filename in script.gpx_offsets:
            last_offset = script.gpx_offsets[filename]

        if last_offset is not None:
            offsets[filename] = last_offset
            last_offset += raw_footage[filename]

    if len(offsets) != len(script.gpx_offsets):
        raise Exception("At least one gpx_offset couldn’t be found in "
                        "raw footage")

    for filename in reversed(sorted(raw_footage.keys())):
        last_offset -= raw_footage[filename]

        if filename not in offsets:
            offsets[filename] = last_offset

    return offsets

def write_speed_script_for_video(f,
                                 script,
                                 video,
                                 gpx_offset,
                                 video_input_time,
                                 video_speeds):
    print(("# {}\n"
           "speed {{").format(video.filename),
          file=f)

    if script.show_elevation:
        print("        elevation", file=f)
    if script.show_map:
        print("        map", file=f)

    print("        file \"speed.gpx\"", file=f)

    input_time = 0
    output_time = 0
    key_frames = []

    if video.end_time:
        video_length = video.end_time - video.start_time
    else:
        video_length = video.length - video.start_time

    def add_frame(input_time, output_time, speed):
        frame = round(output_time * FPS)

        # If the time rounds to the same frame as the previous one
        # then replace it instead
        if len(key_frames) > 0 and frame == key_frames[-1][0]:
            key_frames.pop()

        fps = round(FPS * speed)
        utc_time = gpx_offset + input_time - video_input_time + video.start_time

        key_frames.append((frame, fps, utc_time))

    last_vs = None

    for vs in video_speeds:
        if input_time >= video_input_time + video_length:
            break

        last_vs = vs

        if input_time + vs.length > video_input_time:
            clip_start_time = max(input_time, video_input_time)
            add_frame(clip_start_time,
                      output_time + (clip_start_time - input_time) * vs.speed,
                      vs.speed)

        input_time += vs.length
        output_time += vs.length * vs.speed

    add_frame(video_input_time + video_length,
              output_time +
              (video_input_time + video_length - input_time) *
              last_vs.speed,
              last_vs.speed)

    for frame, fps, utc_time in key_frames:
        print("        key_frame {} {{ fps {} timestamp {} }}".format(
            frame,
            fps,
            utc_time),
              file=f)

    print("}\n", file=f)

def write_speed_script(f, script, video_speeds):
    if len(script.gpx_offsets) == 0:
        return

    offsets = get_video_gpx_offsets(script)
    input_time = 0

    for video in script.videos:
        if video.use_gpx:
            bn = os.path.basename(video.filename)
            write_speed_script_for_video(f,
                                         script,
                                         video,
                                         offsets[bn],
                                         input_time,
                                         video_speeds)

        if video.end_time:
            input_time += video.end_time - video.start_time
        else:
            input_time += video.length - video.start_time

def write_videos_script(f, videos, video_speeds):
    script_time_re = re.compile(r'\bkey_frame\s+(?P<time>' +
                                TIME_RE.pattern +
                                r')')

    for video in videos:
        if len(video.script) == 0:
            continue

        def replace_video_time(md):
            t = decode_time(md.group('time'))
            ot = get_output_time(videos, video_speeds, video.filename, t)
            return (md.group(0)[:(md.start('time') - md.start(0))] +
                    str(round(ot * FPS)))

        print(script_time_re.sub(replace_video_time, "\n".join(video.script)),
              file=f)

if len(sys.argv) >= 2:
    with open(sys.argv[1], "rt", encoding="utf-8") as f:
        script = parse_script(f)
else:
    script = parse_script(sys.stdin)

video_speeds = get_video_speeds(script.videos, script.speed_overrides)
total_video_time = sum(vs.length * vs.speed for vs in video_speeds)

with open("sound.sh", "wt", encoding="utf-8") as f:
    write_sound_script(f,
                       total_video_time,
                       get_sound_clips(script.videos, video_speeds))

os.chmod("sound.sh", 0o775)

with open("scores.flt", "wt", encoding="utf-8") as f:
    print("#!{}".format(os.path.join(os.path.dirname(sys.argv[0]),
                                     "build",
                                     "flootay")),
          file=f)
    write_score_script(f, script.scores, script.videos, video_speeds)
    write_svg_script(f, script.svgs, script.videos, video_speeds)
    write_speed_script(f, script, video_speeds)
    write_videos_script(f, script.videos, video_speeds)

os.chmod("scores.flt", 0o775)

print("\n".join(get_ffmpeg_args(script.videos, video_speeds)))
