#!/usr/bin/python3

import collections
import re
import sys
import subprocess
import shlex

Video = collections.namedtuple('Video', ['filename',
                                         'start_time',
                                         'end_time',
                                         'sounds'])
Sound = collections.namedtuple('Sound', ['start_time', 'filename'])
Clip = collections.namedtuple('Clip',
                              ['filename', 'start_time', 'length', 'fast'])

TIME_RE = re.compile(r'(?:([0-9]+):)?([0-9]+)')

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
    videos = []
    
    for line_num, line in enumerate(infile):
        line = line.strip()

        if len(line) <= 0 or line[0] == '#':
            continue
 
        md = sound_re.match(line)

        if md:
            if len(videos) <= 0:
                raise ParseError(("line {}: sound specified "
                                  "with no video").format(line_num + 1))

            start_time = decode_time(md.group('time'))

            sound = Sound(start_time, md.group('filename'))
            videos[-1].sounds.append(sound)
        else:
            md = video_re.match(line)

            start_time = md.group('start_time')
            if start_time:
                start_time = decode_time(start_time)

            end_time = md.group('end_time')
            if end_time:
                end_time = decode_time(end_time)

            videos.append(Video(md.group('filename'), start_time, end_time, []))

    return videos

def get_video_length(filename):
    s = subprocess.check_output(["ffprobe",
                                 "-i", filename,
                                 "-show_entries", "format=duration",
                                 "-v", "quiet",
                                 "-of", "csv=p=0"])
    return float(s)                                 

def get_clips(videos):
    clips = []

    overlap = 0

    for video in videos:
        last_pos = 0

        if video.start_time is not None:
            last_pos = video.start_time

        if video.end_time is not None:
            video_end_time = video.end_time
        else:
            video_end_time = get_video_length(video.filename)

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

    return clips

def get_ffmpeg_input_args(clip):
    args = []

    if clip.start_time > 0:
        args.extend(["-ss", str(clip.start_time)])

    if clip.length is not None:
        args.extend(["-t", str(clip.length)])

    args.extend(["-i", clip.filename])

    return args

def get_ffmpeg_filter(clips):
    parts = []

    for clip_num, clip in enumerate(clips):
        if not clip.fast:
            continue

        if len(parts) > 0:
            parts.append(";")

        parts.append("[{}:v]setpts=0.333333333333*PTS[f{}]".format(
            clip_num, clip_num))

    if len(parts) > 0:
        parts.append(";")

    for clip_num, clip in enumerate(clips):
        if clip.fast:
            parts.append("[f{}]".format(clip_num))
        else:
            parts.append("[{}:v]".format(clip_num))

    parts.append("concat=n={}:v=1:a=0[outv]".format(len(clips)))

    return "".join(parts)

def get_ffmpeg_args(clips):
    input_args = sum((get_ffmpeg_input_args(clip) for clip in clips), [])
    filter = get_ffmpeg_filter(clips)

    return input_args + ["-filter_complex", filter, "-map", "[outv]"]
                
print(" ".join(shlex.quote(arg) for arg in
               get_ffmpeg_args(get_clips(parse_script(sys.stdin)))))
