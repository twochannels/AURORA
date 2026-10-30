# The front page's three recordings

Each is the host build playing a script (tests/host.c: `T midi ST D1 D2`, `T hold ID`, `T enc ID N`), filmed at 30 fps,
then ffmpeg to a 720 x 720 mp4 with its sound. From the repo's root, with `build/host/aurora` built:

    for n in sky horns glass; do
      build/host/aurora build/demo/$n tools/demo/$n.txt 30 > /dev/null
      ffmpeg -y -r 30 -i build/demo/$n-%05d.ppm -i build/demo/$n.wav -vf "scale=720:720:flags=neighbor" \
             -c:v libx264 -pix_fmt yuv420p -crf 18 -c:a aac -b:a 192k -shortest web/site/demo/aurora-$n.mp4
    done

The glass one is lifted 3 dB (`-af volume=3dB`). The posters are a frame each: sky at 38 s, horns at 80 s, glass at 5 s.
