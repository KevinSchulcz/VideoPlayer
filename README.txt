Video Player
By: Kevin Schulcz

Description:
    A program to display a video file at a specified frame rate in a 
    simple window. A sample video is in the repo.

To Compile:
    Note - Requires GTK4, FFmpeg, and pkg-config for lib managment. All 
            can be installed using Homebrew if on Mac.
    
    gcc $(pkg-config --cflags gtk4 libavformat libavcodec libavutil libswscale) -o VideoPlayer VideoPlayer.c $(pkg-config --libs gtk4 libavformat libavcodec libavutil libswscale)

    Note - If FFmpeg is installed with Homebrew and pkg-config can't find 
            its location, tell it where to search with this command:
    
    export PKG_CONFIG_PATH=/opt/homebrew/opt/ffmpeg/lib/pkgconfig:$PKG_CONFIG_PATH


To Run:

    ./VideoPlayer <video filename> <frame rate>

    Example:
    ./VideoPlayer sample.mp4 30
