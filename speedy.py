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
import json
import io
from xml.etree import ElementTree

class Video:
    def __init__(self, raw_video, start_time, end_time):
        self.raw_video = raw_video
        self.start_time = start_time
        self.end_time = end_time
        self.sounds = []
        self.script = []
        self.filter = []
        self.use_gpx = not raw_video.is_proc and not raw_video.is_image

class RawVideo:
    def __init__(self, filename, length=None):
        self.filename = filename
        self.is_image = re.search(r'\.(?:jpe?g|png)$', filename) is not None
        self.is_proc = filename.startswith("|")

        self.width = None
        self.height = None

        if self.is_proc or self.is_image:
            self.length = self
        else:
            self.video_info = get_video_info(filename)
            self.length = float(self.video_info['format']['duration'])
            for stream in self.video_info['streams']:
                if 'width' in stream:
                    self.width = int(stream['width'])
                    self.height = int(stream['height'])
                    break

Script = collections.namedtuple('Script', ['width',
                                           'height',
                                           'videos',
                                           'scores',
                                           'svgs',
                                           'extra_script',
                                           'gpx_offsets',
                                           'speed_overrides',
                                           'show_elevation',
                                           'show_map',
                                           'twitter',
                                           'sound_args',
                                           'default_speed'])
Svg = collections.namedtuple('Svg', ['video',
                                     'filename',
                                     'start_time',
                                     'length'])
Sound = collections.namedtuple('Sound', ['start_time', 'filename', 'length'])
SpeedOverride = collections.namedtuple('SpeedOverride',
                                       ['raw_video',
                                        'start_time',
                                        'length',
                                        'speed'])
# Length is the time in seconds of the source video, ie, not accelerated
VideoSpeed = collections.namedtuple('VideoSpeed', ['length', 'speed'])
# Filename can be None if silence should be played
SoundClip = collections.namedtuple('SoundClip', ['filename', 'length'])
ScoreDiff = collections.namedtuple('ScoreDiff', ['video', 'time', 'diff'])

TIME_RE = re.compile(r'(?:([0-9]+):)?([0-9]+)(\.[0-9]+)?')
GOPRO_FILENAME_RE = re.compile(r'(?P<camera_type>G[HX])'
                               r'(?P<chapter>[0-9]{2})'
                               r'(?P<file_number>[0-9]{4})\.mp4\Z',
                               flags=re.I)

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
                               r')\s+(?P<utc_time>\S+)'
                               r'(?:\s+(?P<gpx_filename>\S.*))?$')
    slow_re = re.compile(r'slow(?:\s+(?P<start_time>' +
                         TIME_RE.pattern +
                         r')(?:\s+(?P<end_time>' +
                         TIME_RE.pattern +
                         r'))?)?$')
    speed_re = re.compile(r'(?P<speed>[0-9]+(?:\.[0-9]+)?)x(?:\s+'
                          r'(?P<start_time>' +
                          TIME_RE.pattern +
                          r')(?:\s+(?P<end_time>' +
                          TIME_RE.pattern +
                          r'))?)?$')
    sound_args_re = re.compile(r'sound_args\s+(?P<args>.*)')
    filter_re = re.compile(r'filter\s+(?P<filter>.*)')
    output_size_re = re.compile(r'output_size\s+([0-9]+)x([0-9]+)$')
    default_speed_re = re.compile(r'default_speed\s+'
                                  r'(?P<speed>[0-9]+(?:\.[0-9]+)?)x?$')
    flootay_file_re = re.compile(r'flootay_file\s+(?P<filename>.*?)\s*$')

    output_width = 1920
    output_height = 1080
    raw_videos = {}
    videos = []
    scores = []
    svgs = []
    extra_script = []
    speed_overrides = []
    sound_args = []
    gpx_offsets = {}
    show_elevation = False
    show_map = False
    twitter = False
    default_speed = 1.0 / 3.0

    in_script = False

    def maybe_add_gpx_offset(filename):
        gpx_filename = os.path.splitext(filename)[0] + ".gpx"

        if not os.path.exists(gpx_filename):
            return

        tree = ElementTree.parse(gpx_filename)

        md = re.match(r'({http://www\.topografix\.com/GPX/1/[01]})gpx\Z',
                      tree.getroot().tag)

        if md == None:
            raise ParseError(("{} does not appear to be "
                              "a GPX file").format(gpx_filename))

        xpath = "./{}trk/{}trkseg/{}trkpt/{}time".replace("{}", md.group(1))
        first_time = tree.getroot().find(xpath)
        timestamp = dateutil.parser.parse(first_time.text).timestamp()
        gpx_offsets[os.path.basename(filename)] = (timestamp, gpx_filename)

    def get_raw_video(filename, length):
        try:
            return raw_videos[filename]
        except KeyError:
            raw_video = RawVideo(filename, length)
            raw_videos[filename] = raw_video

            maybe_add_gpx_offset(filename)

            return raw_video

    def add_speed_override_from_md(speed, md):
        if md.group('start_time') is None:
            start_time = videos[-1].start_time
        else:
            start_time = decode_time(md.group('start_time'))

        if md.group('end_time') is None:
            if videos[-1].end_time is None:
                end_time = videos[-1].raw_video.length
            else:
                end_time = videos[-1].end_time
        else:
            end_time = decode_time(md.group('end_time'))

        speed_overrides.append(SpeedOverride(videos[-1].raw_video,
                                             start_time,
                                             end_time - start_time,
                                             speed))
    
    for line_num, line in enumerate(infile):
        line = line.strip()

        if in_script:
            if line == "}}":
                in_script = False
            elif len(videos) == 0:
                extra_script.append(line)
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
            raw_video = get_raw_video(video_filename, 3)
            video = Video(raw_video, 0, 3)
            videos.append(video)
            sound_filename = os.path.join(os.path.dirname(sys.argv[0]),
                                          "logo-sound.flac")
            video.sounds.append(Sound(0,
                                      sound_filename,
                                      get_sound_length(sound_filename)))
            speed_overrides.append(SpeedOverride(raw_video,
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

        if line == "twitter":
            twitter = True
            continue

        if line == "no_gpx":
            videos[-1].use_gpx = False
            continue

        md = flootay_file_re.match(line)
        if md:
            with open(md.group('filename'), "rt", encoding="utf-8") as f:
                contents = f.read()
            if len(videos) == 0:
                extra_script.append(contents)
            else:
                videos[-1].script.append(contents)
            continue

        md = output_size_re.match(line)
        if md:
            output_width = int(md.group(1))
            output_height = int(md.group(2))
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
            add_speed_override_from_md(1.0, md)
            continue

        md = speed_re.match(line)
        if md:
            add_speed_override_from_md(1.0 / float(md.group('speed')), md)
            continue

        md = default_speed_re.match(line)
        if md:
            default_speed = 1.0 / float(md.group('speed'))
            continue

        md = gpx_offset_re.match(line)
        if md:
            timestamp = dateutil.parser.parse(md.group('utc_time'))
            offset = (timestamp.timestamp() -
                      decode_time(md.group('video_time')))
            gpx_filename = md.group('gpx_filename') or "speed.gpx"
            gpx_offsets[md.group('filename')] = (offset, gpx_filename)
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
            length = get_sound_length(filename)

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
            potential_raw_length = end_time - start_time
        else:
            potential_raw_length = None

        raw_video = get_raw_video(filename, potential_raw_length)

        videos.append(Video(raw_video, start_time, end_time))

    return Script(output_width,
                  output_height,
                  videos,
                  scores,
                  svgs,
                  extra_script,
                  gpx_offsets,
                  speed_overrides,
                  show_elevation,
                  show_map,
                  twitter,
                  sound_args,
                  default_speed)

def script_has_sound(script):
    if len(script.sound_args) > 0:
        return True

    for video in script.videos:
        if len(video.sounds) > 0:
            return True

    return False

def get_video_info(filename):
    with subprocess.Popen(["ffprobe",
                           "-i", filename,
                           "-show_entries", "format=duration:stream",
                           "-v", "quiet",
                           "-of", "json"],
                          stdout=subprocess.PIPE) as p:
        info = json.load(p.stdout)

    return info

def get_sound_length(filename):
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
            length = video.raw_video.length - video.start_time
        else:
            length = video.end_time - video.start_time

        total_length += length

    return total_length

def get_input_time(videos, raw_video, t):
    total_time = 0

    for video in videos:
        if video.end_time is None:
            end_time = video.raw_video.length
        else:
            end_time = video.end_time

        if (raw_video == video.raw_video and
            t >= video.start_time and
            t < end_time):
            return total_time + t - video.start_time

        total_time += end_time - video.start_time

    raise Exception("Couldn’t find input time in {} at {}".
                    format(raw_video.filename, t))

def get_output_time(videos, video_speeds, raw_video, time):
    input_time = get_input_time(videos, raw_video, time)
    total_input_time = 0
    total_output_time = 0

    for vs in video_speeds:
        if input_time < total_input_time + vs.length:
            return (total_output_time +
                    (input_time - total_input_time) *
                    vs.speed)

        total_input_time += vs.length
        total_output_time += vs.length * vs.speed

    raise Exception("Couldn’t find output time in {} at {}".
                    format(raw_video.filename, time))

def get_video_speeds(videos, speed_overrides):
    total_input_length = get_videos_length(videos)

    times = [[get_input_time(videos, st.raw_video, st.start_time),
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
                video_speeds.append(VideoSpeed(t[0] - last_time,
                                               script.default_speed))

            video_speeds.append(VideoSpeed(t[1], t[2]))
            last_time = t[0] + t[1]

    if last_time < total_input_length:
        video_speeds.append(VideoSpeed(total_input_length - last_time,
                                       script.default_speed))

    return video_speeds

def get_sound_clips(videos, video_speeds):
    sound_clips = []
    sound_pos = 0

    for video in videos:
        for sound in video.sounds:
            sound_clip_pos = get_output_time(videos,
                                             video_speeds,
                                             video.raw_video,
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

def get_ffmpeg_input_args(script, video):
    args = []

    if video.start_time > 0:
        args.extend(["-ss", str(video.start_time)])

    if video.end_time is not None:
        args.extend(["-to", str(video.end_time)])

    if video.raw_video.is_proc:
        args.extend(["-f",
                     "rawvideo",
                     "-pixel_format",
                     "rgb32",
                     "-video_size",
                     "{}x{}".format(script.width, script.height),
                     "-framerate",
                     "30"])
    elif video.raw_video.is_image:
        args.extend(["-framerate", "30", "-loop", "1"])

        if video.end_time is None:
            raise "Missing end time on infinite image input"

    args.extend(["-i", video.raw_video.filename])

    return args

def get_video_speed_filter(video_speeds, is_audio=False):
    input_time = 0
    output_time = 0
    parts = []

    if is_audio:
        parts.append("a")

    parts.append("setpts='")

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
    parts.append("',")
    if is_audio:
        parts.append("a")
    else:
        parts.append("fps=fps={},".format(FPS))
    parts.append("trim=duration={}".format(output_time))

    return "".join(parts)

def get_ffmpeg_filter(script, overlay_input, video_speeds):
    parts = []

    has_sound = script_has_sound(script)

    input_names = ["[{}:v]".format(i) for i in range(len(script.videos))]

    for i, video in enumerate(script.videos):
        video_parts = []

        if len(video.filter):
            video_parts.extend(video.filter)

        if (video.raw_video.width is not None and
            (video.raw_video.width != script.width or
             video.raw_video.height != script.height)):
            video_parts.append("scale={}:{}".format(script.width,
                                                    script.height))

        if len(video_parts) > 0:
            parts.append("[{}]{}[sv{}];".format(i, ",".join(video_parts), i))
            input_names[i] = "[sv{}]".format(i)

        if len(video.script) > 0:
            parts.append("{}[{}]overlay[ov{}];".format(
                input_names[i],
                overlay_input,
                i))
            overlay_input += 1
            input_names[i] = "[ov{}]".format(i)

    if has_sound:
        parts.append("".join(input_names))
    else:
        for i, input_name in enumerate(input_names):
            parts.append("{}[{}:a]".format(input_name, i))

    parts.append("concat=n={}:v=1:a={}".format(len(script.videos),
                                               int(not has_sound)))

    if (len(video_speeds) > 1 or
        (len(video_speeds) == 1 and video_speeds[0].speed != 1)):

        if has_sound:
            parts.append(",")
            parts.append(get_video_speed_filter(video_speeds))
            parts.append("[outv]")
        else:
            parts.append("[coutv][couta];[coutv]")
            parts.append(get_video_speed_filter(video_speeds))
            parts.append("[outv];[couta]")
            parts.append(get_video_speed_filter(video_speeds, is_audio=True))
            parts.append("[outa]")
    else:
        parts.append("[outv]")

        if not has_sound:
            parts.append("[outa]")

    return "".join(parts)

def get_ffmpeg_command(script, video_filename, video_speeds):
    input_args = (["ffmpeg"] +
                  sum((get_ffmpeg_input_args(script, video)
                       for video in script.videos),
                      []))

    next_input = len(script.videos)

    first_overlay_input = next_input
    for video_num, video in enumerate(script.videos):
        if len(video.script) == 0:
            continue

        input_args.extend(["-f", "rawvideo",
                           "-pixel_format", "rgba",
                           "-video_size", "{}x{}".format(script.width,
                                                         script.height),
                           "-framerate", "30",
                           "-i", "|./overlay-{}.flt".format(video_num)])
        next_input += 1

    flootay_input = next_input
    next_input += 1
    input_args.extend(["-f", "rawvideo",
                       "-pixel_format", "rgba",
                       "-video_size", "{}x{}".format(script.width,
                                                     script.height),
                       "-framerate", "30",
                       "-i", "|./overlay.flt"])

    has_sound = script_has_sound(script)

    if has_sound:
        sound_input = next_input
        next_input += 1
        input_args.extend(["-ar", "48000",
                           "-ac", "2",
                           "-channel_layout", "stereo",
                           "-f", "s24le",
                           "-c:a", "pcm_s24le",
                           "-i", "|./sound.sh"])

    filter = (get_ffmpeg_filter(script, first_overlay_input, video_speeds) +
              ";" +
              "[outv][{}]overlay[overoutv]".format(flootay_input))

    args = input_args + ["-filter_complex", filter,
                         "-map", "[overoutv]"]

    args.append("-map")

    if has_sound:
        args.append("{}:a".format(sound_input))
    else:
        args.append("[outa]")

    args.extend(["-r", "30",
                 "-c:v", "libx264",
                 "-pix_fmt", "yuv420p"])

    if script.twitter:
        args.extend(["-profile:v", "main",
                     "-crf", "24"])
    else:
        args.extend(["-profile:v", "high",
                     "-bf", "2",
                     "-g", "30",
                     "-crf", "18"])

    args.append(video_filename)

    return args

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
                               score.video.raw_video,
                               score.time)
        print("        key_frame {} {{ v {} }}".format(time, value),
              file=f)

    end_time = sum(vs.length * vs.speed for vs in video_speeds)

    print("        key_frame {} {{ v {} }}\n".format(end_time, value) +
          "}\n",
          file=f)

def write_svg_script(f, svgs, videos, video_speeds):
    for svg in svgs:
        start_time = get_output_time(videos,
                                     video_speeds,
                                     svg.video.raw_video,
                                     svg.start_time)

        print(("svg {{\n"
               "        file \"{}\"\n"
               "        key_frame {} {{ }}\n"
               "        key_frame {} {{ }}\n"
               "}}\n").format(svg.filename,
                              start_time,
                              start_time + svg.length),
              file=f)

def filename_sort_key(filename):
    # GoPro using an annoying filename system where the chapter
    # appears before the file number so the order is all wrong when
    # sorted. This changes the order to make it sortable
    md = GOPRO_FILENAME_RE.match(filename)

    if md is None:
        return filename

    return (md.group('camera_type').upper() +
            md.group('file_number') +
            md.group('chapter') +
            '.mp4')

def get_video_gpx_offsets(script):
    raw_footage = dict((os.path.basename(video.raw_video.filename),
                        video.raw_video.length)
                       for video in script.videos
                       if video.use_gpx)
    sorted_filenames = list(sorted(raw_footage.keys(), key=filename_sort_key))

    last_offset = None
    offsets = {}
    found_count = 0

    for filename in sorted_filenames:
        if filename in script.gpx_offsets:
            last_offset = script.gpx_offsets[filename]
            found_count += 1

        if last_offset is not None:
            offsets[filename] = last_offset
            last_offset = (last_offset[0] + raw_footage[filename],
                           last_offset[1])

    if found_count != len(script.gpx_offsets):
        raise Exception("At least one gpx_offset couldn’t be found in "
                        "raw footage")

    for filename in reversed(sorted_filenames):
        last_offset = (last_offset[0] - raw_footage[filename], last_offset[1])

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
           "speed {{").format(video.raw_video.filename),
          file=f)

    if script.show_elevation:
        print("        elevation", file=f)
    if script.show_map:
        print("        map", file=f)

    print("        file \"{}\"".format(gpx_offset[1]), file=f)

    input_time = 0
    output_time = 0
    key_frames = []

    if video.end_time:
        video_length = video.end_time - video.start_time
    else:
        video_length = video.raw_video.length - video.start_time

    def add_frame(input_time, output_time, speed):
        # If the time is the same as the previous one then replace it
        # instead
        if len(key_frames) > 0 and output_time == key_frames[-1][0]:
            key_frames.pop()

        utc_time = (gpx_offset[0] +
                    input_time -
                    video_input_time +
                    video.start_time)

        key_frames.append((output_time, utc_time))

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

    for timestamp, utc_time in key_frames:
        print("        key_frame {} {{ timestamp {} }}".format(
            timestamp,
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
            bn = os.path.basename(video.raw_video.filename)
            write_speed_script_for_video(f,
                                         script,
                                         video,
                                         offsets[bn],
                                         input_time,
                                         video_speeds)

        if video.end_time:
            input_time += video.end_time - video.start_time
        else:
            input_time += video.raw_video.length - video.start_time

def write_video_script(f, video):
    script_time_re = re.compile(r'\bkey_frame\s+(?P<time>' +
                                TIME_RE.pattern +
                                r')')

    if video.end_time:
        end_time = video.end_time
    else:
        end_time = video.raw_video.length

    def replace_video_time(md):
        t = decode_time(md.group('time')) - video.start_time
        if t < 0:
            raise Exception("Time in flootay script is negative "
                            "for {} in {}".format(
                                md.group('time'), video.raw_video.filename))
        if t > end_time:
            raise Exception("Time in flootay script is past end of video "
                            "for {} in {}".format(
                                md.group('time'), video.raw_video.filename))
        return md.group(0)[:(md.start('time') - md.start(0))] + str(t)

    print(script_time_re.sub(replace_video_time, "\n".join(video.script)),
          file=f)

if len(sys.argv) >= 2:
    video_filename = os.path.splitext(sys.argv[1])[0] + ".mp4"
    with open(sys.argv[1], "rt", encoding="utf-8") as f:
        script = parse_script(f)
else:
    video_filename = "film.mp4"
    script = parse_script(sys.stdin)

video_speeds = get_video_speeds(script.videos, script.speed_overrides)
total_video_time = sum(vs.length * vs.speed for vs in video_speeds)

if script_has_sound(script):
    with open("sound.sh", "wt", encoding="utf-8") as f:
        write_sound_script(f,
                           total_video_time,
                           get_sound_clips(script.videos, video_speeds))

    os.chmod("sound.sh", 0o775)

flootay_proc = os.path.join(os.path.dirname(sys.argv[0]),
                            "build",
                            "flootay")
flootay_header = ("#!{}\n"
                  "\n"
                  "video_width {}\n"
                  "video_height {}\n").format(flootay_proc,
                                              script.width, script.height)

with open("overlay.flt", "wt", encoding="utf-8") as f:
    print(flootay_header + "\n".join(script.extra_script), file=f)
    write_score_script(f, script.scores, script.videos, video_speeds)
    write_speed_script(f, script, video_speeds)
    write_svg_script(f, script.svgs, script.videos, video_speeds)

os.chmod("overlay.flt", 0o775)

for video_num, video in enumerate(script.videos):
    if len(video.script) == 0:
        continue

    filename = "overlay-{}.flt".format(video_num)
    with open(filename, "wt", encoding="utf-8") as f:
        print(flootay_header, file=f)
        write_video_script(f, video)
    os.chmod(filename, 0o775)

print(os.path.join(os.path.dirname(sys.argv[0]),
                   "build",
                   "run-ffmpeg"),
      " ".join(shlex.quote(arg)
               for arg in get_ffmpeg_command(script,
                                             video_filename,
                                             video_speeds)))
