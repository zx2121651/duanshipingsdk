#version 310 es
        // 声明每个工作组内有 16x16 个计算线程 (Invocation)
        layout(local_size_x = 16, local_size_y = 16) in;

        // 绑定输入输出的 Image (无需经过 sampler 纹理过滤，直接读取裸数据)
        layout(binding = 0, rgba8) uniform readonly highp image2D inputImage;
        layout(binding = 1, rgba8) uniform writeonly highp image2D outputImage;

        uniform float blurSize;

        void main() {
            // 获取当前计算线程对应的像素坐标
            ivec2 texelPos = ivec2(gl_GlobalInvocationID.xy);
            ivec2 size = imageSize(inputImage);

            // 越界保护
            if (texelPos.x >= size.x || texelPos.y >= size.y) {
                return;
            }

            vec4 sum = vec4(0.0);
            int count = 0;
            int radius = int(blurSize);

            // 粗暴的盒式模糊 (Box Blur) 演示并行算力
            for(int y = -radius; y <= radius; y++) {
                for(int x = -radius; x <= radius; x++) {
                    ivec2 offsetPos = texelPos + ivec2(x, y);
                    // 处理边界 clamp
                    offsetPos.x = clamp(offsetPos.x, 0, size.x - 1);
                    offsetPos.y = clamp(offsetPos.y, 0, size.y - 1);

                    sum += imageLoad(inputImage, offsetPos);
                    count++;
                }
            }

            vec4 result = sum / float(count);
            // 将计算结果直接写入显存的输出纹理中
            imageStore(outputImage, texelPos, result);
        }