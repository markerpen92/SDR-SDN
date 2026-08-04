#!/bin/bash
# Cross-compile mesh-emu-m13-OpenWifi for OpenWiFi (ARM soft-float)
# NS3_ROOT 預設由本腳本位置推導（scratch/openwifi-ops → ns-3 根目錄）
# History pattern: CXX=arm-linux-gnueabi-g++ + static debug + -j 1 (OOM-safe)
#
#   cd /path/to/ns-3.37
#   ./scratch/openwifi-ops/cross-build.sh
#   OPENWIFI_HOST=root@192.168.10.122 ./scratch/openwifi-ops/cross-build.sh
set -euo pipefail

OPS_DIR="$(cd "$(dirname "$0")" && pwd)"
NS3_ROOT="${NS3_ROOT:-$(cd "$OPS_DIR/../.." && pwd)}"
TARGET_NAME="ns3.37-mesh-emu-m13-OpenWifi-debug"
OPENWIFI_HOST="${OPENWIFI_HOST:-root@192.168.10.122}"
REMOTE_DIR="${REMOTE_DIR:-~/ns-3/scratch}"
MAX_TRIES="${MAX_TRIES:-5}"

cd "$NS3_ROOT"
echo "NS3_ROOT=$NS3_ROOT"

if ! command -v arm-linux-gnueabi-g++ >/dev/null; then
  echo "缺少 arm-linux-gnueabi-g++，請先安裝："
  echo "  sudo apt install g++-8-arm-linux-gnueabi gcc-8-arm-linux-gnueabi"
  exit 1
fi

echo "[1/4] clean + configure (ARM static debug)"
./ns3 clean || true
CXX="arm-linux-gnueabi-g++" CXXFLAGS_EXTRA="-march=arm" \
  ./ns3 configure --enable-static --build-profile=debug

echo "[2/4] build -j 1 (記憶體不足時請多試幾次)"
ok=0
for i in $(seq 1 "$MAX_TRIES"); do
  echo "=== build attempt $i/$MAX_TRIES ==="
  if ./ns3 build -j 1; then
    ok=1
    break
  fi
  echo "build failed (常為 OOM)。等待 10s 後重試..."
  sleep 10
done
if [[ "$ok" -ne 1 ]]; then
  echo "連續 $MAX_TRIES 次編譯失敗"
  exit 1
fi

BIN="$NS3_ROOT/build/scratch/$TARGET_NAME"
if [[ ! -x "$BIN" ]]; then
  echo "找不到產物: $BIN"
  ls -la "$NS3_ROOT/build/scratch/" | head
  exit 1
fi
file "$BIN"

echo "[3/4] 一併上傳 OpenWiFi 執行腳本"
OPS_DIR="$NS3_ROOT/scratch/openwifi-ops"

echo "[4/4] scp 到 OpenWiFi: $OPENWIFI_HOST:$REMOTE_DIR"
echo "密碼預設為 openwifi"
scp "$BIN" "$OPS_DIR/run-mesh-ap.sh" "$OPENWIFI_HOST:$REMOTE_DIR/"

echo
echo "完成。到板子上執行："
echo "  ssh $OPENWIFI_HOST"
echo "  cd ~/ns-3/scratch && chmod +x run-mesh-ap.sh $TARGET_NAME"
echo "  ./run-mesh-ap.sh"
