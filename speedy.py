#!/usr/bin/python3

import collections
import re
import sys
import subprocess
import shlex
import dateutil.parser

Script = collections.namedtuple('Script', ['videos',
                                           'scores',
                                           'svgs',
                                           'gpx_offset'])
Video = collections.namedtuple('Video', ['filename',
                                         'start_time',
                                         'end_time',
                                         'length',
                                         'sounds'])
Svg = collections.namedtuple('Svg', ['video',
                                     'filename',
                                     'start_time',
                                     'length'])
Sound = collections.namedtuple('Sound', ['start_time', 'filename'])
Clip = collections.namedtuple('Clip',
                              ['filename', 'start_time', 'length', 'fast'])
# Filename can be None if silence should be played
SoundClip = collections.namedtuple('SoundClip', ['filename', 'length'])
ScoreDiff = collections.namedtuple('ScoreDiff', ['video', 'time', 'diff'])

TIME_RE = re.compile(r'(?:([0-9]+):)?([0-9]+)')

SPEED_UP = 1.0 / 3.0

FPS = 30

class ParseError(Exception):
    pass

def decode_time(time_str):
    md = TIME_RE.match(time_str)
    seconds = int(md.group(2))

    if md.group(1):
        seconds += int(md.group(1)) * 60

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
    gpx_offset_re = re.compile(r'gpx_offset\s+(?P<video_time>'
                               + TIME_RE.pattern +
                               r')\s+(?P<utc_time>.*)')
    videos = []
    scores = []
    svgs = []
    gpx_offset = None
    
    for line_num, line in enumerate(infile):
        line = line.strip()

        if len(line) <= 0 or line[0] == '#':
            continue

        md = gpx_offset_re.match(line)
        if md:
            timestamp = dateutil.parser.parse(md.group('utc_time'))
            gpx_offset = (timestamp.timestamp() -
                          decode_time(md.group('video_time')))
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

            sound = Sound(start_time, md.group('filename'))
            videos[-1].sounds.append(sound)
            continue

        md = video_re.match(line)

        filename = md.group('filename')

        start_time = md.group('start_time')
        if start_time:
            start_time = decode_time(start_time)

        end_time = md.group('end_time')
        if end_time:
            end_time = decode_time(end_time)

        videos.append(Video(filename,
                            start_time,
                            end_time,
                            get_video_length(filename),
                            []))

    return Script(videos, scores, svgs, gpx_offset)

def get_video_length(filename):
    s = subprocess.check_output(["ffprobe",
                                 "-i", filename,
                                 "-show_entries", "format=duration",
                                 "-v", "quiet",
                                 "-of", "csv=p=0"])
    return float(s)                                 

def get_clips_length(clips):
    total = 0

    for clip in clips:
        if clip.fast:
            total += clip.length * SPEED_UP
        else:
            total += clip.length

    return total

def get_clips(videos):
    clips = []
    sound_clips = []

    overlap = 0
    sound_pos = 0

    for video in videos:
        last_pos = 0

        if video.start_time is not None:
            last_pos = video.start_time

        if video.end_time is not None:
            video_end_time = video.end_time
        else:
            video_end_time = video.length

        if overlap > 0:
            if overlap > video_end_time - last_pos:
                if video.end_time:
                    clip_end = video.end_time - last_pos
                else:
                    clip_end = None
                clips.append(Clip(video.filename, last_pos, clip_end, False))
                overlap -= video_end_time - last_pos
                continue

            clips.append(Clip(video.filename, last_pos, overlap, False))
            last_pos += overlap
            overlap = 0

        for sound in video.sounds:
            sound_length = get_video_length(sound.filename)

            if sound.start_time > last_pos:
                clips.append(Clip(video.filename,
                                  last_pos,
                                  sound.start_time - last_pos,
                                  True))

            sound_clip_pos = get_clips_length(clips)

            if sound_clip_pos > sound_pos:
                sound_clips.append(SoundClip(None, sound_clip_pos - sound_pos))
                
            sound_clips.append(SoundClip(sound.filename, sound_length))
            sound_pos = sound_clip_pos + sound_length

            if sound.start_time + sound_length > video_end_time:
                if video.end_time:
                    clip_end = video.end_time - sound.start_time
                else:
                    clip_end = None
                clips.append(Clip(video.filename,
                                  sound.start_time,
                                  clip_end,
                                  False))
                overlap = sound.start_time + sound_length - video_end_time
                last_pos = video_end_time
            else:
                clips.append(Clip(video.filename,
                                  sound.start_time,
                                  sound_length,
                                  False))
                last_pos = sound.start_time + sound_length

        if last_pos < video_end_time:
            if video.end_time:
                clip_end = video.end_time - last_pos
            else:
                clip_end = None
            clips.append(Clip(video.filename,
                              last_pos,
                              clip_end,
                              True))

    return clips, sound_clips

def get_ffmpeg_input_args(clip):
    args = []

    if clip.start_time > 0:
        args.extend(["-ss", str(clip.start_time)])

    if clip.length is not None:
        args.extend(["-t", str(clip.length)])

    args.extend(["-i", clip.filename])

    return args

def get_ffmpeg_sound_input_args(clip):
    if clip.filename is None:
        return ["-f", "lavfi", "-t", str(clip.length), "-i", "anullsrc"]
    else:
        return ["-i", clip.filename]

def get_ffmpeg_filter(clips):
    parts = []

    for clip_num, clip in enumerate(clips):
        if not clip.fast:
            continue

        if len(parts) > 0:
            parts.append(";")

        parts.append("[{}:v]setpts={}*PTS[f{}]".format(
            clip_num, SPEED_UP, clip_num))

    if len(parts) > 0:
        parts.append(";")

    for clip_num, clip in enumerate(clips):
        if clip.fast:
            parts.append("[f{}]".format(clip_num))
        else:
            parts.append("[{}:v]".format(clip_num))

    parts.append("concat=n={}:v=1:a=0[outv]".format(len(clips)))

    return "".join(parts)

def get_ffmpeg_sound_filter(sound_clips, first_input):
    inputs = "".join("[{}]".format(i + first_input)
                     for i in range(len(sound_clips)))
    return inputs + "concat=n={}:v=0:a=1[outa]".format(len(sound_clips))

def get_ffmpeg_args(clips, sound_clips):
    input_args = sum((get_ffmpeg_input_args(clip) for clip in clips), [])
    input_args.extend(sum((get_ffmpeg_sound_input_args(clip)
                           for clip in sound_clips),
                          []))
    filter = (get_ffmpeg_filter(clips) +
              ";" +
              get_ffmpeg_sound_filter(sound_clips, len(clips)))

    return input_args + ["-filter_complex", filter,
                         "-map", "[outv]",
                         "-map", "[outa]"]

def get_output_time(filename, time, clips):
    t = 0

    for clip in clips:
        if (filename == clip.filename and
            time >= clip.start_time and
            time < clip.start_time + clip.length):
            off = time - clip.start_time
            if clip.fast:
                off *= SPEED_UP
            return t + off

        if clip.fast:
            t += clip.length * SPEED_UP
        else:
            t += clip.length

    raise Exception("no clip found for {} @ {}".format(filename, time))

def write_score_script(f, scores, clips):
    if len(scores) <= 0:
        return

    print("score {", file=f)

    value = 0

    for score in scores:
        value += score.diff
        time = get_output_time(score.video.filename, score.time, clips)
        print("        key_frame {} {{ v {} }}".format(round(time * FPS),
                                                       value),
              file=f)

    end_time = get_output_time(clips[-1].filename,
                               clips[-1].start_time + clips[-1].length - 0.01,
                               clips)
    print("        key_frame {} {{ v {} }}\n".format(round(end_time * FPS),
                                                     value) +
          "}\n",
          file=f)

def write_svg_script(f, svgs, clips):
    for svg in svgs:
        start_time = get_output_time(svg.video.filename, svg.start_time, clips)

        print(("svg {{\n"
               "        file \"{}\"\n"
               "        key_frame {} {{ }}\n"
               "        key_frame {} {{ }}\n"
               "}}\n").format(svg.filename,
                              round(start_time * FPS),
                              round((start_time + svg.length) * FPS)),
              file=f)

def write_speed_script(f, script, clips):
    if script.gpx_offset is None:
        return

    print("speed {\n"
          "        file \"speed.gpx\"",
          file=f)

    videos = list(sorted(set((video.filename, video.length)
                             for video in script.videos)))

    for clip in clips:
        video_time = 0

        for video in videos:
            if video[0] == clip.filename:
                break
            video_time += video[1]

        utc_time = round(script.gpx_offset + video_time + clip.start_time)
        out_time = get_output_time(clip.filename, clip.start_time, clips)
        fps = round(FPS * SPEED_UP) if clip.fast else FPS

        print("        key_frame {} {{ fps {} timestamp {} }}".format(
            round(out_time * FPS),
            fps,
            utc_time),
              file=f)

    end_time = get_output_time(clips[-1].filename,
                               clips[-1].start_time + clips[-1].length - 0.01,
                               clips)
    print(("        key_frame {} {{ }}\n"
           "}}\n").format(round(end_time * FPS)),
          file=f)

if len(sys.argv) >= 2:
    with open(sys.argv[1], "rt", encoding="utf-8") as f:
        script = parse_script(f)
else:
    script = parse_script(sys.stdin)

clips, sound_clips = get_clips(script.videos)

with open("scores.flt", "wt", encoding="utf-8") as f:
    write_score_script(f, script.scores, clips)
    write_svg_script(f, script.svgs, clips)
    write_speed_script(f, script, clips)

print("\n".join(get_ffmpeg_args(clips, sound_clips)))
