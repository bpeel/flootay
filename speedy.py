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

    def end_time_or_length(self):
        if self.end_time is None:
            return self.raw_video.length
        else:
            return self.end_time

    def length(self):
        return self.end_time_or_length() - self.start_time

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

class Script:
    def __init__(self):
        self.width = 1920
        self.height = 1080
        self.videos = []
        self.scores = []
        self.svgs = []
        self.extra_script = []
        self.speed_overrides = []
        self.sound_args = []
        self.gpx_offsets = {}
        self.show_elevation = False
        self.show_distance = False
        self.show_map = False
        self.show_time = False
        self.twitter = False
        self.instagram = False
        self.default_speed = 1.0 / 3.0
        self.silent = False
        self.background_sound = False
        self.distance_offset = None
        self.dial = False
        self.text_color = None
        self.map_trace = None
        self.map_trace_color = None

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

OVERLAY_FILTER = "overlay=eof_action=pass"

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
    distance_offset_re = re.compile(r'distance_offset\s+'
                                    r'(?P<offset>-?[0-9]+(?:\.[0-9]+)?)$')
    text_color_re = re.compile(r'text_color\s+(?P<color>.*)')
    map_trace_re = re.compile(r'map_trace\s+(?P<filename>\S+)\s*$')
    map_trace_color_re = re.compile(r'map_trace_color\s+(?P<color>\S+)\s*$')

    raw_videos = {}
    script = Script()

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
        script.gpx_offsets[os.path.basename(filename)] = (timestamp,
                                                          gpx_filename)

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
            start_time = script.videos[-1].start_time
        else:
            start_time = decode_time(md.group('start_time'))

        if md.group('end_time') is None:
            end_time = script.videos[-1].end_time_or_length()
        else:
            end_time = decode_time(md.group('end_time'))

        script.speed_overrides.append(SpeedOverride(script.videos[-1].raw_video,
                                                    start_time,
                                                    end_time - start_time,
                                                    speed))
    
    for line_num, line in enumerate(infile):
        line = line.strip()

        if in_script:
            if line == "}}":
                in_script = False
            elif len(script.videos) == 0:
                script.extra_script.append(line)
            else:
                script.videos[-1].script.append(line)
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
            script.videos.append(video)
            sound_filename = os.path.join(os.path.dirname(sys.argv[0]),
                                          "logo-sound.flac")
            video.sounds.append(Sound(0,
                                      sound_filename,
                                      get_sound_length(sound_filename)))
            script.speed_overrides.append(SpeedOverride(raw_video,
                                                        0, # start_time
                                                        3, # end_time
                                                        1.0))
            continue

        if line == "elevation":
            script.show_elevation = True
            continue

        if line == "distance":
            script.show_distance = True
            continue

        if line == "map":
            script.show_map = True
            continue

        if line == "time":
            script.show_time = True
            continue

        if line == "dial":
            script.dial = True
            continue

        if line == "twitter":
            script.twitter = True
            continue

        if line == "instagram":
            script.instagram = True
            continue

        if line == "silent":
            script.silent = True
            continue

        if line == "background_sound":
            script.background_sound = True
            continue

        if line == "no_gpx":
            script.videos[-1].use_gpx = False
            continue

        md = flootay_file_re.match(line)
        if md:
            with open(md.group('filename'), "rt", encoding="utf-8") as f:
                contents = f.read()
            if len(script.videos) == 0:
                script.extra_script.append(contents)
            else:
                script.videos[-1].script.append(contents)
            continue

        md = output_size_re.match(line)
        if md:
            script.width = int(md.group(1))
            script.height = int(md.group(2))
            continue

        md = filter_re.match(line)
        if md:
            script.videos[-1].filter.append(md.group('filter'))
            continue

        md = sound_args_re.match(line)
        if md:
            script.sound_args.extend(shlex.split(md.group('args')))
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
            script.default_speed = 1.0 / float(md.group('speed'))
            continue

        md = distance_offset_re.match(line)
        if md:
            script.distance_offset = float(md.group('offset'))
            continue

        md = text_color_re.match(line)
        if md:
            script.text_color = md.group('color')
            continue

        md = gpx_offset_re.match(line)
        if md:
            timestamp = dateutil.parser.parse(md.group('utc_time'))
            offset = (timestamp.timestamp() -
                      decode_time(md.group('video_time')))
            gpx_filename = md.group('gpx_filename') or "speed.gpx"
            script.gpx_offsets[md.group('filename')] = (offset, gpx_filename)
            continue

        md = svg_re.match(line)
        if md:
            if len(script.videos) <= 0:
                raise ParseError(("line {}: svg specified "
                                  "with no video").format(line_num + 1))

            script.svgs.append(Svg(script.videos[-1],
                                   md.group('filename'),
                                   decode_time(md.group('start_time')),
                                   decode_time(md.group('length'))))

            continue
 
        md = score_re.match(line)
        if md:
            if len(script.videos) <= 0:
                raise ParseError(("line {}: score specified "
                                  "with no video").format(line_num + 1))

            script.scores.append(ScoreDiff(script.videos[-1],
                                           decode_time(md.group('time')),
                                           int(md.group('diff'))))
            continue

        md = sound_re.match(line)
        if md:
            if len(script.videos) <= 0:
                raise ParseError(("line {}: sound specified "
                                  "with no video").format(line_num + 1))

            start_time = decode_time(md.group('time'))
            filename = md.group('filename')
            length = get_sound_length(filename)

            sound = Sound(start_time, filename, length)
            script.videos[-1].sounds.append(sound)

            continue

        md = map_trace_re.match(line)
        if md:
            script.map_trace = md.group('filename')
            continue

        md = map_trace_color_re.match(line)
        if md:
            script.map_trace_color = md.group('color')
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

        script.videos.append(Video(raw_video, start_time, end_time))

    return script

def script_has_sound(script):
    if len(script.sound_args) > 0:
        return True

    for video in script.videos:
        if len(video.sounds) > 0:
            return True

    return False

def is_normal_speed(video_speeds):
    return len(video_speeds) == 1 and video_speeds[0].speed == 1

def get_sound_mode(script, video_speeds):
    if script.silent:
        return "silence"

    if script_has_sound(script):
        return "generate"

    if is_normal_speed(video_speeds):
        return "original"

    return "silence"

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
        total_length += video.length()

    return total_length

def get_input_time(videos, raw_video, t):
    total_time = 0

    for video in videos:
        end_time = video.end_time_or_length()

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

def get_video_speed_filter(video_speeds):
    input_time = 0
    output_time = 0
    parts = []

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
    parts.append("fps=fps={},".format(FPS))
    parts.append("trim=duration={}".format(output_time))

    return "".join(parts)

def get_ffmpeg_filter(script,
                      sound_mode,
                      overlay_input,
                      silent_input,
                      video_speeds):
    parts = []

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
            if overlay_input is None:
                parts.append("{}flootay=filename=overlay-{}.flt[ov{}];".format(
                    input_names[i],
                    i,
                    i))
            else:
                parts.append("{}[{}]{}[ov{}];".format(
                    input_names[i],
                    overlay_input,
                    OVERLAY_FILTER,
                    i))
                overlay_input += 1

            input_names[i] = "[ov{}]".format(i)

    if sound_mode == "original":
        for i, input_name in enumerate(input_names):
            if script.videos[i].raw_video.is_image:
                audio_input = silent_input
                silent_input += 1
            else:
                audio_input = i
            parts.append("{}[{}:a]".format(input_name, audio_input))
    else:
        parts.append("".join(input_names))

    parts.append("concat=n={}:v=1:a={}".format(len(script.videos),
                                               int(sound_mode == "original")))

    if is_normal_speed(video_speeds):
        parts.append("[outv]")

        if sound_mode == "original":
            parts.append("[outa]")
    else:
        parts.append(",")
        parts.append(get_video_speed_filter(video_speeds))
        parts.append("[outv]")

    return "".join(parts)

def check_ffmpeg_has_flootay():
    filters = subprocess.check_output(["ffmpeg", "-hide_banner", "-filters"],
                                      encoding="utf-8")
    return re.search(r'^\s*...\s+flootay\s+', filters, re.MULTILINE) is not None

def get_silent_source(duration):
    return ["-f", "lavfi",
            "-i",
            "anullsrc=channel_layout=stereo:"
            "sample_rate=48000:"
            f"d={duration}"]

def get_ffmpeg_command(script, video_filename, video_speeds):
    input_args = (["ffmpeg"] +
                  sum((get_ffmpeg_input_args(script, video)
                       for video in script.videos),
                      []))

    next_input = len(script.videos)

    sound_mode = get_sound_mode(script, video_speeds)

    has_flootay = check_ffmpeg_has_flootay()

    if has_flootay:
        first_overlay_input = None
    else:
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

    sound_input = next_input

    if sound_mode == "generate":
        next_input += 1
        input_args.extend(["-ar", "48000",
                           "-ac", "2",
                           "-channel_layout", "stereo",
                           "-f", "s24le",
                           "-c:a", "pcm_s24le",
                           "-i", "|./sound.sh"])
    elif sound_mode == "original":
        for video in script.videos:
            if not video.raw_video.is_image:
                continue

            input_args.extend(get_silent_source(0))
            next_input += 1

    if has_flootay:
        overlay_filter = "[outv]flootay=filename=overlay.flt[overoutv]"
    else:
        flootay_input = next_input
        next_input += 1
        input_args.extend(["-f", "rawvideo",
                           "-pixel_format", "rgba",
                           "-video_size", "{}x{}".format(script.width,
                                                         script.height),
                           "-framerate", "30",
                           "-i", "|./overlay.flt"])
        overlay_filter = "[outv][{}]{}[overoutv]".format(flootay_input,
                                                         OVERLAY_FILTER)

    filter = (get_ffmpeg_filter(script,
                                sound_mode,
                                first_overlay_input,
                                sound_input,
                                video_speeds) +
              ";" +
              overlay_filter)

    args = input_args + ["-filter_complex", filter,
                         "-map", "[overoutv]"]

    if sound_mode == "generate":
        args.extend(["-map", "{}:a".format(sound_input)])
    elif sound_mode == "original":
        args.extend(["-map", "[outa]"])
    elif sound_mode == "silence":
        args.append("-an")

    args.extend(["-r", "30",
                 "-c:v", "libx264",
                 "-pix_fmt", "yuv420p"])

    if script.twitter:
        args.extend(["-profile:v", "main",
                     "-crf", "24"])
    elif script.instagram:
        args.extend(["-profile:v", "main",
                     "-level:v", "3.0",
                     "-x264-params", "scenecut=0:open_gop=0:min-keyint=72:keyint=72:ref=4",
                     "-crf", "23",
                     "-maxrate", "3500k",
                     "-bufsize", "3500k",
                     "-r", "30"])
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

    if script.background_sound:
        print(" -m background-sound.flac", end='', file=f)

    pos = 0

    for clip in sound_clips:
        if clip.filename:
            print(" -s {} {}".format(pos, shlex.quote(clip.filename)),
                  end='',
                  file=f)
        pos += clip.length

    print("", file=f)

def write_score_script(f, script, video_speeds):
    if len(script.scores) <= 0:
        return

    print("score {", file=f)

    if script.text_color is not None:
        print("        color {}\n".format(script.text_color),
              file=f)

    value = 0

    for score in script.scores:
        value += score.diff
        time = get_output_time(script.videos,
                               video_speeds,
                               score.video.raw_video,
                               score.time)
        print("        key_frame {} {{ v {} }}".format(time, value),
              file=f)

    end_time = sum(vs.length * vs.speed for vs in video_speeds)

    print("        key_frame {} {{ v {} }}\n".format(end_time, value) +
          "}\n",
          file=f)

def write_svg_script(f, script, video_speeds):
    for svg in script.svgs:
        start_time = get_output_time(script.videos,
                                     video_speeds,
                                     svg.video.raw_video,
                                     svg.start_time)

        print(("svg {{\n"
               "        file \"{}\"\n"
               "        key_frame {} {{ x1 0 y1 0 x2 {} y2 {} }}\n"
               "        key_frame {} {{ }}\n"
               "}}\n").format(svg.filename,
                              start_time,
                              script.width,
                              script.height,
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
    mp4_re = re.compile(r'\.mp4$', re.IGNORECASE)

    all_mp4 = [(fn, get_sound_length(fn))
               for fn in os.listdir()
               if mp4_re.search(fn)]

    def tuple_sort_key(pair):
        return (filename_sort_key(pair[0]), pair[1])
    all_mp4.sort(key=tuple_sort_key)

    last_offset = None
    offsets = {}

    for filename, length in all_mp4:
        if filename in script.gpx_offsets:
            last_offset = script.gpx_offsets[filename]

        if last_offset is not None:
            offsets[filename] = last_offset
            last_offset = (last_offset[0] + length,
                           last_offset[1])

    for filename, length in reversed(all_mp4):
        last_offset = (last_offset[0] - length, last_offset[1])

        if filename not in offsets:
            offsets[filename] = last_offset

    return offsets

def get_speed_script_for_video(script,
                               video,
                               gpx_offset):
    parts = ["gpx {\n"]

    def add_part(name, extra=None):
        parts.append("        {} {{\n".format(name))
        if script.text_color is not None:
            parts.append("        color {}\n".format(script.text_color))
        if extra is not None:
            parts.append(extra)
        parts.append("        }\n")

    if script.dial:
        speed_extra = ("                dial \"{}\"\n"
                       "                needle \"{}\"\n"
                       "                width {}\n"
                       "                height {}\n"
                       "                full_speed {}\n").format(
                           os.path.join(os.path.dirname(sys.argv[0]),
                                        "dial.svg"),
                           os.path.join(os.path.dirname(sys.argv[0]),
                                        "needle.svg"),
                           script.height / 5.0,
                           script.height / 5.0,
                           # max speed on the dial is 40km/h for a
                           # rotation of 270°. We need to convert that
                           # to a scale of 360° in m/s
                           40.0 * 360 / 270 * 1000 / 3600)
    else:
        speed_extra = None

    add_part("speed", speed_extra)

    if script.show_elevation:
        add_part("elevation")
    if script.show_distance:
        if script.distance_offset is not None:
            distance_extra = ("        offset {}\n".format(
                script.distance_offset))
        else:
            distance_extra = None
        add_part("distance", distance_extra)
    if script.show_map:
        parts.append("        map {\n")
        if script.map_trace:
            parts.append(f"                trace \"{script.map_trace}\"\n")
            if script.map_trace_color:
                parts.append("                trace_color "
                             f"{script.map_trace_color}\n")
        parts.append("        }\n")

    timestamp = gpx_offset[0] + video.start_time

    parts.append(("        file \"{}\"\n"
                  "        key_frame {} {{ timestamp {} }}\n"
                  "        key_frame {} {{ timestamp {} }}\n"
                  "}}\n").format(gpx_offset[1],
                                 video.start_time,
                                 timestamp,
                                 video.end_time_or_length(),
                                 timestamp + video.length()))

    return "".join(parts)

def add_speed_scripts(script):
    if len(script.gpx_offsets) == 0:
        return

    offsets = get_video_gpx_offsets(script)

    for video in script.videos:
        if video.use_gpx:
            bn = os.path.basename(video.raw_video.filename)
            video.script.append(get_speed_script_for_video(script,
                                                           video,
                                                           offsets[bn]))

def add_time_scripts(script):
    timestamp = 0

    for video in script.videos:
        if video.raw_video.is_proc or video.raw_video.is_image:
            continue

        length = video.length()

        video.script.append("time {\n")

        if script.text_color is not None:
            video.script.append("        color {}\n".format(script.text_color))

        video.script.append(("        key_frame {} {{ time {} }}\n"
                             "        key_frame {} {{ time {} }}\n"
                             "}}\n").format(video.start_time,
                                            timestamp,
                                            video.end_time_or_length(),
                                            timestamp + length))

        timestamp += length

def write_video_script(f, video):
    script_time_re = re.compile(r'\bkey_frame\s+(?P<time>' +
                                TIME_RE.pattern +
                                r')')

    end_time = video.end_time_or_length()

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

def generate_background_sound(script):
    print("set -eu")

    args = ["ffmpeg"]

    for video_num, video in enumerate(script.videos):
        if video.raw_video.is_image or video.raw_video.is_proc:
            args.extend(get_silent_source(video.length()))
        else:
            if video.start_time > 0:
                args.extend(["-ss", str(video.start_time)])

            if video.end_time is not None:
                args.extend(["-to", str(video.end_time)])

            args.extend(["-i", video.raw_video.filename])

    filter_parts = []

    for i in range(len(script.videos)):
        filter_parts.append(f"[{i}:a]")

    filter_parts.append(f"concat=n={len(script.videos)}:v=0:a=1[outa]")

    args.extend(["-filter_complex", ''.join(filter_parts),
                 "-map", "[outa]",
                 "background-sound.flac"])

    print(" ".join(shlex.quote(arg) for arg in args))

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

add_speed_scripts(script)

if script.show_time:
    add_time_scripts(script)

flootay_proc = os.path.join(os.path.dirname(sys.argv[0]),
                            "build",
                            "flootay")
flootay_header = (("#!{}\n"
                   "\n"
                   "video_width {}\n"
                   "video_height {}\n").format(flootay_proc,
                                               script.width, script.height) +
                  "\n".join(script.extra_script) +
                  "\n")

with open("overlay.flt", "wt", encoding="utf-8") as f:
    print(flootay_header, file=f)
    write_score_script(f, script, video_speeds)
    write_svg_script(f, script, video_speeds)

os.chmod("overlay.flt", 0o775)

for video_num, video in enumerate(script.videos):
    if len(video.script) == 0:
        continue

    filename = "overlay-{}.flt".format(video_num)
    with open(filename, "wt", encoding="utf-8") as f:
        print(flootay_header, file=f)
        write_video_script(f, video)
    os.chmod(filename, 0o775)

if script.background_sound:
    generate_background_sound(script)

print(os.path.join(os.path.dirname(sys.argv[0]),
                   "build",
                   "run-ffmpeg"),
      " ".join(shlex.quote(arg)
               for arg in get_ffmpeg_command(script,
                                             video_filename,
                                             video_speeds)))
