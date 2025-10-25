# VideoPlayer

A C program that displays a video file at a specified frame rate in a simple window.

## Demo

A short demo of the program

## Getting Started

### Dependencies

* GTK4
* FFmpeg
* pkg-config
* A C compiler

### Installing Dependencies

#### On macOS

All dependencies can be installed on macOS easily through Homebrew, or manually if you prefer.
1. Install Homebrew here.
2. Install the 3 libraries with Homebrew.
{
    brew install gtk4
    brew install ffmpeg
    brew install pkgconf
}
3. macOS already comes with a C compiler installed (clang) so installing one isn't necessary.

### Compiling

```
gcc $(pkg-config --cflags gtk4 libavformat libavcodec libavutil libswscale) -o VideoPlayer VideoPlayer.c $(pkg-config --libs gtk4 libavformat libavcodec libavutil libswscale)
```

**Note:** 
If dependencies were installed on macOS with Homebrew, pkg-config might throw 
errors saying it can't find FFmpeg's libraries. If you installed Homebrew with 
all the default options run this to tell pkg-config where to look. (Or replace 
with the path to wherever you saved the files.)
```
export PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH
```

### Executing

```
./VideoPlayer <video filename/path> <frame rate>
```

```
Ex. ./VideoPlayer sample.mp4 30
```

## Authors

Kevin Schulcz