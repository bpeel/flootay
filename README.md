# Flootay

This is a hacky set of tools that I use to make YouTube videos. It generates a film based on a description in a text file that can be written by hand. It’s mainly useful for combining clips of footage from different files and commentating them with sound clips. It’s particularly useful for creating cycling videos because it can add a moving map and a speedometer from a GPX trace. The final video is generated using ffmpeg, so the tools are mainly useful to translate the text description to an ffmpeg command line.

## Building the tools

First install the necessary dependencies. On Fedora this can be done like this:

```bash
sudo dnf install meson ninja-build SDL2{,_image}-devel cairo-devel \
                 librsvg2-devel expat-devel libcurl-devel json-c-devel
```

Or on Ubuntu:

```bash
sudo apt install gcc ninja-build meson git libsdl2{-image,}-dev \
                 libcairo2-dev librsvg2-dev libexpat1-dev libcurl-dev \
                 libjson-c-dev
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
../flootay/speedy.py my-video.script | bash
```

The video will be written to a file with the same name as the script but the extension changed to `.mp4`. It will have a resolution of 1920x1080. Note that it is assumed that all of the input videos have an aspect ratio of 16:9. The videos will be scaled to 1920x1080.

## Speed

By default flootay will speed up all of the videos by three times. You can change the default speed by putting a line like this anywhere in the script:

```
default_speed 2.5
```

You can slow down portions of the video to normal speed with a command like this after the video that you want to slow down.

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

With either command you can leave out the start time or the end time to make it default to the start or end of the clip. So if you leave out both times for example by just writing `slow` somewhere after the video then the whole clip will be at normal speed.

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

### Keeping the original sound

If all of the videos are at the original speed and you don’t add any sound files or `sound_args` lines then speedy will keep the original sound from the videos. You can stop it from doing this by adding the line `silent` somewhere in the script.

## GPX trace

If you want to add a speedometer to your video, save a GPX trace in the same directory as the script and call it `speed.gpx`. You need to give flootay a reference point within one of the films so that it can work out the offsets. In order to do this, put a command like this at the start of the script:

```
gpx_offset part1.mp4 11.299 2022-08-11T145400Z
```

This means that at 11.299 seconds into the video `part1.mp4`, the time and date were the 11th of August 2022 at 14:54 UTC. A good way to work this out could be to film your GPS device at the start of the video and look at the time in a video player. Then you just need to note down the time displayed in the video player and the time displayed on the GPS at that point.

There can be multiple `gpx_offset` commands in the script. If there are multiple videos, flootay will assume they were filmed sequentially and the filenames sort to chronological order. It works like this because my camera splits the recordings up into 15-minute files, so as long as I have an offset for one of the files it will be able to calculate the offset for the rest of them. If a video doesn’t have its own offset specified then the program will calculate one from a previous video. If no previous video has one it will use one from a subsequent video.

Alternatively, if you have a separate GPX file for each video you can place a GPX with the same name as the video but a GPX extension. In that case you don’t need to use `gpx_offset` and speedy will just assume that the first track point in the file is the time for the start of the video. If you are using a GoPro with a GPS, you can extract the GPX data using the [gopro2gpx](https://github.com/juanmcasillas/gopro2gpx) script. For example:

```bash
for x in *.MP4; do
  python3 -m gopro2gpx "$x" $(echo "$x" | sed 's/\.MP4$//')
done
```

### Dial

If you put the word `dial` somewhere in the script then the speedometer will be displayed as a speed dial instead of showing the digits. The dial is built out of the SVGs `dial.svg` and `needle.svg` which you can modify if you want to customize the image.

### Skipping videos

If some of the videos aren’t correlated to the GPX file, you can add `no_gpx` somewhere after that video. That way it won’t have the speedometer displayed on top of that video and the video won’t affect the offset calculation for the GPX file.

### Map

If you write the word `map` on a line anywhere in the script file then the speedometer will be accompanied by a moving mapping at the bottom right of the video. By default the map tiles are sourced from the [OpenStreetMap cycle map](https://www.thunderforest.com/maps/opencyclemap/). You can use a different map source by specifying a URL to a compatible API. For example to use an outdoors hiking map you can add this to your script before the first video:

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

### Trace from Cyclopolis

You can add a trace to the map using a file in the JSON format used by [Cyclopolis](https://cyclopolis.fr), which is a site used to track the progress of the express cycle network in Lyon. Other cities might use the same technology too. The style of the trace will try to match that of the Cyclopolis site where planned sections are a dotted line and WIP sections are animated etc. To add this to your videos add a line like the following somewhere in the script:

```
map_trace ../voieslyonnaises/content/voies-cyclables/ligne-1.json
```

You can change the color of the line like this:

```
map_trace_color 0x60A75B
```

### Distance

If you write the word `distance` on a line somewhere in the script then the total distance travelled will also be displayed at the bottom of the video. The distance is calculated from the first point in the GPX file. You can offset this value with a line like:

```
# value in meters
distance_offset -512
```

### Time

If you write the word `time` on a line in the script then the cumulative time of the unaccelerated videos will be displayed at the top of the output video.

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

The overlay in the video is generated from a separate text file description in the “flootay language”. This is automatically generated from the script file and saved to a file calles `overlay.flt`. You can embed extra goodies in this file by writing them in the source script in a section that starts with `{{` and ends with `}}`.

### Draw a rectangle

I use this to draw a rectangle over licence plates in my videos. For example:

```
{{

rectangle {
        color "red"
        key_frame 1:58.46 { x1 -12 y1 766 x2 132 y2 878 }
        key_frame 1:58.86 { x1 149 y1 766 x2 288 y2 850 }
        key_frame 1:59.26 { x1 449 y1 715 x2 588 y2 818 }
}

}}
```

The color can be a CSS color name in a string, or a hexadecimal value like `color 0xffff00` for yellow. If no color is specified the rectangle will be black.

This adds a rectangle for about 1 second starting from 1:58.46 in the input video. The x1/y1/x2/y2 specify the dimensions of the rectangle. The values will be interpolated for the frames in-between. The rectangle will disappear at the time mentioned in the last key frame.

The times in the script file are the times in the source video. Note that in real flootay language these times need to be relative to the output video, but the program that reads the video script will automatically convert these for you.

You can use the [make key frames](#make-key-frames) program to help pick the key frame numbers.

### Draw an SVG

You can render an SVG like this:

```
{{

svg {
  file "happy-face.svg"
  key_frame 1:00 { x1 30 y1 20 x2 40 y2 30 }
  key_frame 1:20 { }
}

}}
```

This will display the SVG `happy-face.svg` fit into a rectangle at 30,20 with size 10,10 for 20 seconds starting from 1:00 in the input video. Any parameters not mentioned in the key frame will have the same values as the previous frame, so that means the SVG will stay at 30,20. You can also animate the position with something like this:


```
{{

svg {
  file "happy-face.svg"
  key_frame 1:00 { x1 30 y1 20 x2 40 y2 30 }
  key_frame 1:20 { x1 100 y1 90 x2 110 y2 100 }
}

}}
```

That will make the happy face slide across the screen from 30,20 to 100,90. This can be a nice way to cover up faces in the video.

## Make key frames

The `make-key-frames` program can be used to help write the key frames for the flootay language. If you want to cover up a licence plate that appears in a section of a video, run the program like this:

```
make-key-frames -s <start-time> -e <end-time> <video-file>
```

This will use ffmpeg to generate a bunch of snapshots of the video and then open a hacky UI. You can scroll through the snapshots using the mouse wheel or the page up/down keys.

When you want to add a key frame for a snapshot, just drag with the left mouse button to draw it. The box will for this snapshot will be shown in red. The program will also display the box for the last 5 snapshots in blue. If you make a mistake you can just redraw the box or press `d` to delete it.

You can also click with the right mouse button to reposition the box so that it is centred where you clicked. If you do this before drawing a box, it will make a box that has the same size as a previous snapshot. This is useful if you want to avoid changing the box size and just want to animate the movement.

You can also move the box around with the cursor keys to avoid changing its size. If you hold down shift or alt at the same time then the distance the box is moved will be different.

When you are finished drawing the key frames, press `w`. This will copy the description of the key frames into the clipboard. You can then paste this into your script file.

### Snapshot rate

By default the program will generate 10 snapshots per second of video. If your box moves more smoothly or more jaggedly then you can change the number of snapshots per second with the `-r` option on the command line.

### SVGs

When drawing SVGs, you can give flootay a rectangle to fit the image into. The image will be scaled so that it is centered within the rectangle whilst maintaining the aspect ratio. You can specify the SVG file to load with the `-S` option on the command line:

```
make-key-frames -S <svg-file> …
```

This will also cause the SVG to be displayed for the current frame when editing the rectangles. It will also try to read the size of the SVG as the default rectangle size. This means that you can simply right click to position the SVG without having to draw a box.

### Rectangle size

Sometimes you want the size of the rectangle or the SVG to change over the course of the animation. This is useful for example if the thing you are covering up moves closer towards the camera during the animation. It looks nicer if you do this without changing the aspect ratio of the rectangle. In order to help do this, if you hold down shift while drawing a rectangle, it will force the rectangle to keep the same aspect ratio as a previous frame.

In order to avoid sudden jumps in the size, you can press Shift+S before writing out the rectangle data in order to make the editor smooth out the size changes. Whenever it finds a rectangle with the same size a previous rectangle, it will interpolate the size between the surrounding rectangles with different sizes. You probably only want to do this once after you have finished editing all the rectangles.

## FFmpeg filter

By default flootay works by passing raw video frames to ffmpeg via a pipe and then using the overlay filter to apply them. There is also an [experimental branch](https://github.com/bpeel/ffmpeg) of ffmpeg that adds a filter to generate the overlay directly onto the frames from the source clips. This has the advantage that flootay doesn’t need to be told the video size and it will generate the overlay exactly at the right time for each frame of the source video even if it has a variable frame rate. If you build that branch and put the resulting ffmpeg executable in the PATH where speedy can find it then it will automatically detect and use the filter.
