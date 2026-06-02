# Sample Media — 测试向量说明

此目录用于存放 `test_media_compat.cpp` 和 `test_ffmpeg_decoder.cpp` 的真实视频测试素材。

**出于文件大小限制，视频文件不随代码提交。** 请按以下步骤获取：

---

## 推荐测试向量

### 1. Big Buck Bunny（H.264 / 1080p / 60fps）
```bash
# 完整版（~300MB）
curl -fLO https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/1080/Big_Buck_Bunny_1080_10s_30MB.mp4

# 短片段（~3MB，适合 CI）
curl -fLO https://test-videos.co.uk/vids/bigbuckbunny/mp4/h264/360/Big_Buck_Bunny_360_10s_1MB.mp4
```

### 2. GPAC MP4Box 测试向量（H.264 / H.265 / VFR / B帧）
```bash
# H.265 / HEVC
curl -fLO https://github.com/gpac/gpac/raw/master/tests/media/hevc/hevc_360p.mp4

# VFR 可变帧率
curl -fLO https://github.com/gpac/gpac/raw/master/tests/media/misc/vfr.mp4
```

### 3. Rotation Metadata Test（旋转元数据 90/180/270°）
```bash
# MediaInfo 基准测试集
curl -fLO https://github.com/nicowillis/video-rotation-tests/raw/main/rotate90.mp4
curl -fLO https://github.com/nicowillis/video-rotation-tests/raw/main/rotate180.mp4
curl -fLO https://github.com/nicowillis/video-rotation-tests/raw/main/rotate270.mp4
```

---

## 与测试用例的对应关系

| 文件名                             | 测试用例       | 说明                        |
|-----------------------------------|---------------|----------------------------|
| `Big_Buck_Bunny_1080_10s_30MB.mp4` | TC-C01         | H.264 正常帧序列             |
| `hevc_360p.mp4`                    | TC-C02         | H.265 codec 检测             |
| `vfr.mp4`                          | TC-C03         | VFR PTS 单调性验证           |
| `rotate90.mp4`, `rotate180.mp4`    | TC-C04         | 旋转元数据读取               |
| `bad_pts.mp4`（自行构造）           | TC-C05         | 异常 PTS — 用 ffmpeg 制造    |
| `bframe.mp4`（自行构造）            | TC-C06         | B帧 seek 降级测试            |
| *(空路径)*                          | TC-C07         | 无需文件，测试错误码返回      |
| *(多文件同时打开)*                   | TC-C08         | 解码池并发压力               |

---

## 如何构造异常测试向量

### bad_pts.mp4 — 负 PTS / 乱序 PTS
```bash
# 用 ffmpeg 打乱 PTS（仅供测试，产出文件将违反 ISO 标准）
ffmpeg -i Big_Buck_Bunny_360_10s_1MB.mp4 \
    -vf "setpts=PTS-2*TB" -an -c:v copy \
    -movflags +faststart bad_pts.mp4
```

### bframe.mp4 — 仅含 B 帧（禁用参考帧）
```bash
ffmpeg -i Big_Buck_Bunny_360_10s_1MB.mp4 \
    -vf "select=between(n\,0\,59)" -c:v libx264 \
    -x264opts "bframes=8:no-weightb" \
    -an bframe.mp4
```

---

## CI 建议

在 CI/CD 管线中，可用以下命令批量下载（约 8MB）：

```bash
./scripts/download_test_media.sh   # TODO: 可参考 download_ffmpeg_android.sh 格式添加
```

或者仅运行 stub 模式的单元测试（不依赖真实文件）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest -R test_media_compat --output-on-failure
```
