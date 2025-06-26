# QT_ffmpeg_rtsp_4k
Use qt and ffmpeg to pull 4k rtsp video streams and realize recording and screenshot functions

I developed it with vs2019 and qt tools. You can add my files directly to the qt project. Of course, you also need the dependency of ffmpeg.

This is just a simple practice, the performance is not good, the video stream displaying 4k-30FPS is quite smooth, if you need more powerful functions, you need to use GPU decoding and implementation directly, of course, this is very complicated. My current solution is GPU-CPU-GPU, the maximum occupancy on 1660S is 50, and the 10th generation i7 CPU occupancy is less than 10%. Finally, there is another flaw that my refresh rate is fixed at 33ms using a timer, which is sometimes not accurate.

Finally, I wish you a happy life.
