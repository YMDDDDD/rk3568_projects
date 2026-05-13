#!/bin/bash
# ============================================================================
# 部署到开发板
# 用法: ./deploy.sh [device_serial]
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/app/build"
BINARY_NAME="rk3568-camera"
TARGET_DIR="/usr/bin"
MODEL_DIR="/usr/share/rk3568-camera/model"

DEVICE_SERIAL="${1:-ded4884b71d327f3}"

echo "Deploying to device: ${DEVICE_SERIAL}"

# 推送可执行文件
echo "  -> pushing ${BINARY_NAME} to ${TARGET_DIR}"
adb -s "${DEVICE_SERIAL}" push "${BUILD_DIR}/${BINARY_NAME}" "${TARGET_DIR}/${BINARY_NAME}"

# 推送模型文件
echo "  -> pushing model files"
adb -s "${DEVICE_SERIAL}" shell "mkdir -p ${MODEL_DIR}"
adb -s "${DEVICE_SERIAL}" push "${PROJECT_DIR}/model/" "${MODEL_DIR}/" 2>/dev/null || true

# 推送自启动脚本
echo "  -> pushing startup script"
adb -s "${DEVICE_SERIAL}" shell "cat > /etc/init.d/S99camera << 'SCRIPT_EOF'
#!/bin/sh
case \"\$1\" in
  start)
    echo \"Starting rk3568-camera...\"
    export QT_QPA_PLATFORM=eglfs
    export QT_QPA_EGLFS_INTEGRATION=eglfs_kms
    /usr/bin/rk3568-camera &
    ;;
  stop)
    echo \"Stopping rk3568-camera...\"
    killall rk3568-camera 2>/dev/null || true
    ;;
  *)
    echo \"Usage: \$0 {start|stop}\"
    exit 1
    ;;
esac
SCRIPT_EOF
chmod +x /etc/init.d/S99camera"

echo "Deploy complete!"
