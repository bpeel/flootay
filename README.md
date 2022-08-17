# Flootay

This is a hacky set of tools that I use to make YouTube videos. It generates a film based on a description in a text file that can be written by hand. It’s mainly useful for combining clips of footage from different files and commentating them with sound clips. It’s particularly useful for creating cycling videos because it can add a moving map and a speedometer from a GPX trace. The final video is generated using ffmpeg, so the tools are mainly useful to translate the text description to an ffmpeg command line.

## Building the tools

First install the necessary dependencies. On Fedora this can be done like this:

```bash
sudo dnf install meson ninja-build SDL2{,_image}-devel cairo-devel \
                 librsvg2-devel expat-devel
```

In order to run the tools you will also need to [install ffmpeg](https://computingforgeeks.com/how-to-install-ffmpeg-on-fedora/). On Fedora this will involve enabling the RPM fusion repository in order to get the various codecs.

You can then build the tools like this:

```bash
meson build
ninja -C build
```

## Videos

The main utility of the script file is to list a set of videos to compose into the output video. You can optionally specify a start and an end time for each video. The times can be a number of seconds, or a combination of minutes and seconds.

```
# Include the whole of part1.mp4
part1.mp4
# Include the first eleven minutes and 15 seconds of part2.mp4
part2.mp4 0 11:15
# Include the whole of part3.mp4 starting from 70.25 seconds
part3.mp4 70.25
```

If you save this into a file called `my-video.script` you could then generate the video like this:

```
../flootay/build/generate-film my-video.script
```

The video will be written to `film.mov`. It will have a resolution of 1920x1080. Note that it is assumed that all of the input videos have an aspect ratio of 16:9. The videos will be scaled to 1920x1080. The output video will be compressed with the ProRES codec on a high quality setting. This will make the file very large but it is supposed to be a good format to upload to YouTube so that it won’t get too garbled when YouTube reencodes it.

## Speed

By default flootay will speed up all of the videos by three times. You can slow down portions of the video to normal speed with a command like this after the video that you want to slow down.

```
part1.mp4

# Slow down the video to normal speed between the 1st minute and 2.5th
# minute of part1.mp4
slow 1:00 2:30
```

Note that the times are the times in the source video, not the output video, so you can figure out where to slow down by watching the video in a video player and noting down the time at the interesting points.

You can also set any other arbitrary speed. For example, if you want to speed up a part where you are just waiting for a traffic light for 3 minutes starting from the 5th minute, you can do this:

```
part1.mp4

# Speed up a boring bit of part1.mp4 starting from the 5th minute.
8x 5:00 8:00
```

## Sound

You can add commentary to a video by splitting your comments up into separate audio files. For example:

```
part1.mp4

1:15 the-m12-sign.flac

3:00 the-badly-parked-car.flac
```

That will play the first sound file at 1.25 minutes into the first video file, and the second sound file at 3 minutes into the video. As with the speeds above, these times are the times in the source video file, so you can work out what to write by looking at the source video in a video player and noting down the time at the point where you want the audio to start.

### Music

If you want to add background music to the video, you can tell flootay about the music files like this:

```
sound_args -m never-gonna-give-you-up.mp3
sound_args -m flashdance.mp3
sound_args -m popcorn.mp3
```

The three pieces of music will be looped to fill the length of the video. At the end of the video the music will automatically fade out. The music will also be automatically quieter whenever a regular audio file is playing at the same time.

If you don’t want the music to start immediately at the beginning of the video, add a command like this:

```
sound_args -S 73
```

This will make the music start at the 73rd second.

## GPX trace

If you want to add a speedometer to your video, save a GPX trace in the same directory as the script and call it `speed.gpx`. The trace needs to have the speed recorded in it for this to work. If you have a GPX file without the speed, you can synthesise the speed with [gpsbabel](https://www.gpsbabel.org/) with a command like this:

```
gpsbabel -t -i gpx -f my-trace.gpx -x track,speed -o gpx -F speed.gpx
```

You need to give flootay a reference point within one of the films so that it can work out the offsets. In order to do this, put a command like this at the start of the script:

```
gpx_offset part1.mp4 11.299 2022-08-11T145400Z
```

This means that at 11.299 seconds into the video `part1.mp4`, the time and date were the 11th of August 2022 at 14:54 UTC. A good way to work this out could be to film your GPS device at the start of the video and look at the time in a video player. Then you just need to note down the time displayed in the video player and the time displayed on the GPS at that point.

There can be multiple `gpx_offset` commands in the script. If there are multiple videos, flootay will assume they were filmed sequentially and the filenames sort to chronological order. It works like this because my camera splits the recordings up into 15-minute files, so as long as I have an offset for one of the files it will be able to calculate the offset for the rest of them. If a video doesn’t have its own offset specified then the program will calculate one from a previous video. If no previous video has one it will use one from a subsequent video.

### Skipping videos

If some of the videos aren’t correlated to the GPX file, you can add `no_gpx` somewhere after that video. That way it won’t have the speedometer displayed on top of that video and the video won’t affect the offset calculation for the GPX file.

### Map

If you write the word `map` on a line anywhere in the script file then the speedometer will be accompanied by a moving mapping at the bottom right of the video. By default the map tiles are sourced from the [OpenStreetMap cycle map](https://www.thunderforest.com/maps/opencyclemap/). You can use a different map source by specifying a URL to a compatible API. For example to use an outdoors hiking map you can add this to your script:

```
{{
map_url_base "https://tile.thunderforest.com/outdoors/"
}}
```

The ``{{ }}`` brackets are because this part is in the flootay language which will be explained later.

If you don’t use an API key, the map tiles will probably end up being watermarked with “API key required”. If you [create an API key](https://manage.thunderforest.com/) you can inform flootay of it like this:

```
{{
map_api_key "cafecafecafecafecafecafecafecafe"
}}
```

The map tiles will be downloaded once and cached on disk for later runs. If you want to redownload them, delete the `map-tiles` directory.

## Image videos

If the video filename ends in `.jpg` or `.png` it will be repeated to fill the length of the video. For this to work the end time has to be specified. For example:

```
map-of-my-city.png 0 10
```

This will show `map-of-my-city.png` as if it was a video for 10 seconds.

## External process videos

You can also specify an external process to generate a video if the video filename starts with `|`. The process needs to output raw video frames on stdout in `rgb32` format at 1920x1080 and 30 FPS. You need to specify the length of the video for this to work. For example:

```
|../../flootay/build/generate-logo 0 3
```

### Logo

The above example runs the builtin logo generation program. There is also a shortcut to do this by adding a line that is just `logo`. However, don’t do this, because it will add my logo to your videos and that would just be weird. You might however want to look at [generate-logo.c](generate-logo.c) for inspiration on how to generate video.

## Flootay language

The overlay in the video is generated from a separate text file description in the “flootay language”. This is automatically generated from the script file and saved to a file calles `scores.flt`. You can embed extra goodies in this file by writing them in the source script in a section that starts with `{{` and ends with `}}`.

### Draw a rectangle

I use this to draw a black rectangle over licence plates in my videos. For example:

```
{{

rectangle {
        key_frame 1:58.46 { x1 -12 y1 766 x2 132 y2 878 }
        key_frame 1:58.86 { x1 149 y1 766 x2 288 y2 850 }
        key_frame 1:59.26 { x1 449 y1 715 x2 588 y2 818 }
}

}}
```

This adds a rectangle for about 1 second starting from 1:58.46 in the input video. The x1/y1/x2/y2 specify the dimensions of the rectangle. The values will be interpolated for the frames in-between. The rectangle will disappear at the time mentioned in the last key frame.

The times in the script file are the times in the source video. Note that in real flootay language these times need to be frame numbers, but the program that reads the video script will automatically convert these for you.

### Draw an SVG

You can render an SVG like this:

```
{{

svg {
  file "happy-face.svg"
  key_frame 1:00 { x 30 y 20 }
  key_frame 1:20 { }
}

}}
```

This will display the SVG `happy-face.svg` at 30,20 for 20 seconds starting from 1:00 in the input video. Any parameters not mentioned in the key frame will have the same values as the previous frame, so that means the SVG will stay at 30,20. You can also animate the position with something like this:


```
{{

svg {
  file "happy-face.svg"
  key_frame 1:00 { x 30 y 20 }
  key_frame 1:20 { x 100 y 90 }
}

}}
```

That will make the happy face slide across the screen from 30,20 to 100,90. This can be a nice way to cover up faces in the video.


