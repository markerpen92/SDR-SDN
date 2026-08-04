#!/bin/bash
# 發送端：把 H.264/RTP 送到 OpenWiFi ingress (預設 192.168.10.111:5004)
# 用法（在任意 ns-3 安裝路徑下）：
#   ./scratch/openwifi-ops/send-from-226.sh
#   HOST=192.168.10.111 IFACE=eth0 FILE=/path/to/video.h264 ./scratch/openwifi-ops/send-from-226.sh
set -euo pipefail

OPS_DIR="$(cd "$(dirname "$0")" && pwd)"
NS3_ROOT="${NS3_ROOT:-$(cd "$OPS_DIR/../.." && pwd)}"
HOST="${HOST:-192.168.10.111}"
PORT="${PORT:-5004}"
FILE="${FILE:-$NS3_ROOT/scratch/rtp-exp/naruto.h264}"
IFACE="${IFACE:-}"
AUTO_INSTALL="${AUTO_INSTALL:-1}"

GST_PKGS=(
  gstreamer1.0-tools
  gstreamer1.0-plugins-base
  gstreamer1.0-plugins-good
  gstreamer1.0-plugins-bad
  gstreamer1.0-plugins-ugly
  gstreamer1.0-libav
  python3-gi
  python3-gi-cairo
  gir1.2-gstreamer-1.0
)

need_install=0
if ! command -v gst-inspect-1.0 >/dev/null 2>&1; then
  need_install=1
elif ! gst-inspect-1.0 h264parse >/dev/null 2>&1; then
  need_install=1
elif ! gst-inspect-1.0 rtph264pay >/dev/null 2>&1; then
  need_install=1
fi

if [[ "$need_install" -eq 1 ]]; then
  echo "缺少 GStreamer H.264/RTP 元件（例如 h264parse）。"
  if [[ "$AUTO_INSTALL" != "1" ]]; then
    echo "請手動安裝：sudo apt-get install -y ${GST_PKGS[*]}"
    exit 1
  fi
  echo "自動安裝：${GST_PKGS[*]}"
  sudo apt-get update -qq
  sudo apt-get install -y "${GST_PKGS[@]}"
  if ! gst-inspect-1.0 h264parse >/dev/null 2>&1; then
    echo "安裝後仍找不到 h264parse，請檢查 apt 來源後重試。"
    exit 1
  fi
fi

if [[ ! -f "$FILE" ]]; then
  if [[ -f "$NS3_ROOT/scratch/rtp-exp/naruto.mp4" ]]; then
    echo "找不到 naruto.h264，改用 gst 從 mp4 編碼送出"
    FILE="$NS3_ROOT/scratch/rtp-exp/naruto.mp4"
    USE_MP4=1
  else
    echo "找不到影片: $FILE"
    echo "請放置 Annex-B H.264 到 scratch/rtp-exp/，或："
    echo "  FILE=/path/to/video.h264 $0"
    exit 1
  fi
else
  USE_MP4=0
fi

if [[ "$USE_MP4" == "1" ]] && ! gst-inspect-1.0 x264enc >/dev/null 2>&1; then
  echo "mp4 路徑需要 x264enc，安裝 gstreamer1.0-plugins-ugly ..."
  sudo apt-get install -y gstreamer1.0-plugins-ugly
fi

# 自動選通往 HOST 的網卡；可用 IFACE=xxx 覆寫
if [[ -z "$IFACE" ]]; then
  IFACE=$(ip -4 route get "$HOST" 2>/dev/null | awk '{for (i = 1; i <= NF; i++) if ($i == "dev") { print $(i + 1); exit }}' || true)
fi
if [[ -z "$IFACE" ]]; then
  echo "無法自動判斷網卡，請設定 IFACE=（例如 eth0 / enp0s31f6）"
  exit 1
fi

echo "NS3_ROOT=$NS3_ROOT"
echo "Sender iface=$IFACE -> $HOST:$PORT  file=$FILE"
ip -br addr show "$IFACE" 2>/dev/null || ip addr show "$IFACE" || true

# 可選：若封包進不了 emu，依 NS-3 印出的 MAC 設定
# sudo ip neigh replace "$HOST" lladdr aa:bb:cc:dd:ee:ff dev "$IFACE"

cd "$NS3_ROOT/scratch/rtp-exp"

if [[ "$USE_MP4" == "1" ]]; then
  exec gst-launch-1.0 -e \
    filesrc location="$FILE" ! decodebin ! x264enc tune=zerolatency bitrate=1000 speed-preset=ultrafast ! \
    rtph264pay config-interval=1 pt=96 ! udpsink host="$HOST" port="$PORT" sync=true
fi

exec python3 send-rtp.py --host "$HOST" --port "$PORT" "$FILE"
