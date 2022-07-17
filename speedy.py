#!/usr/bin/python3

import collections
import re
import sys
import subprocess
import shlex

Video = collections.namedtuple('Video', ['filename', 'sounds'])
Sound = collections.namedtuple('Sound', ['start_time', 'filename'])
Clip = collections.namedtuple('Clip',
                              ['filename', 'start_time', 'length', 'fast'])

class ParseError(Exception):
    pass

def parse_script(infile):
    sound_re = re.compile(r'(?:([0-9]+):)?([0-9]+)\s+(.*)')
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

            start_time = int(md.group(2))

            if md.group(1):
                start_time += int(md.group(1)) * 60

            sound = Sound(start_time, md.group(3))
            videos[-1].sounds.append(sound)
        else:
            videos.append(Video(line, []))

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
        video_length = get_video_length(video.filename)
        last_pos = 0

        if overlap > 0:
            if overlap > video_length:
                clips.append(Clip(video.filename, 0, None, False))
                overlap -= video_length
                continue

            clips.append(Clip(video.filename, 0, video_length, False))
            last_pos = overlap
            overlap = 0

        for sound in video.sounds:
            sound_length = get_video_length(sound.filename)

            if sound.start_time > last_pos:
                clips.append(Clip(video.filename,
                                  last_pos,
                                  sound.start_time - last_pos,
                                  True))

            if sound.start_time + sound_length > video_length:
                clips.append(Clip(video.filename,
                                  sound.start_time,
                                  None,
                                  False))
                overlap = sound.start_time + sound_length - video_length
                last_pos = video_length
            else:
                clips.append(Clip(video.filename,
                                  sound.start_time,
                                  sound_length,
                                  False))
                last_pos = sound.start_time + sound_length

        if last_pos < video_length:
            clips.append(Clip(video.filename,
                              last_pos,
                              None,
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
