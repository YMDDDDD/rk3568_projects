
## 纯 DRM Plane 显示和OpenGL有什么区别
### Mali OpenGL和OpenGL OpenGL ES有什么区别

## 紧凑RAW与非紧凑RAW的核心区别

## V4L2 mmap buffer 为什么是零拷贝

## rk3568有哪些相关的硬件

## VLC显示延迟大约3s(屏幕秒表法)

可能的原因：
1. VLC 默认网络缓存太大（1000ms+）
2. 编码器输出队列积压
3. RTP 发送队列积压
4. TCP 传输有阻塞