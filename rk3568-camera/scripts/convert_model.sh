#!/bin/bash
# ============================================================================
# ONNX → RKNN 模型转换脚本
# 需要 rknn-toolkit2 已安装
# 用法: ./convert_model.sh [onnx_path] [output_path]
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MODEL_DIR="${PROJECT_DIR}/model"

ONNX_PATH="${1:-${MODEL_DIR}/yolov5s.onnx}"
OUTPUT_PATH="${2:-${MODEL_DIR}/yolov5s.rknn}"

echo "Converting YOLOv5s ONNX to RKNN..."
echo "  Input:  ${ONNX_PATH}"
echo "  Output: ${OUTPUT_PATH}"

python3 - << PYEOF
from rknn.api import RKNN

rknn = RKNN(verbose=True)

# 配置
rknn.config(
    mean_values=[[0, 0, 0]],
    std_values=[[255, 255, 255]],
    target_platform='rk3568',
    quantized_dtype='asymmetric_quantized-8',
    optimization_level=3
)

# 加载 ONNX
ret = rknn.load_onnx(model='${ONNX_PATH}')
if ret != 0:
    print('Load ONNX failed!')
    exit(1)

# 构建 RKNN
ret = rknn.build(do_quantization=True, dataset='./dataset.txt')
if ret != 0:
    print('Build RKNN failed!')
    exit(1)

# 导出
ret = rknn.export_rknn('${OUTPUT_PATH}')
if ret != 0:
    print('Export RKNN failed!')
    exit(1)

print('Conversion complete: ${OUTPUT_PATH}')

# 验证
ret = rknn.init_runtime(target='rk3568')
if ret == 0:
    print('Runtime verification: OK')
    rknn.release()
else:
    print('Warning: runtime verification failed (expected without NPU device)')
PYEOF

echo "Done!"
