# ipcam: connect IP CAM in local network
Enable "IPCAM_RTP".

# Simulation: send rtp stream by ffmpeg
Disable "IPCAM_RTP" and run the following command in terminal
```
ffmpeg -stream_loop -1 -re -i 1024600.mp4 \
       -map 0:v:0 \
       -an \
       -c:v libx264 \
       -preset ultrafast \
       -tune zerolatency \
       -f rtp \
       -sdp_file stream.sdp \
       rtp://192.168.0.2:50004
```

Note: The local vc8k lib modified for FreeRTOS is used to prevent task preemption during the decoding process. It is recommended to replace the global one with the local version for better performance.