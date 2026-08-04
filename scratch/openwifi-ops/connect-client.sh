#!/bin/bash
# Client：連上 OpenWiFi AP (SSID=openwifi)，固定 192.168.13.188，啟動 RTP 接收
#
# 建議先把下列檔案拷到 client 家目錄（或任意目錄後設定 RECV_SCRIPT）：
#   connect-client.sh
#   recv-rtp.py   ← 來自 ns-3 的 scratch/rtp-exp/recv-rtp.py
set -euo pipefail

SSID="${SSID:-openwifi}"
CONN_NAME="${CONN_NAME:-openwifi}"
IFACE="${IFACE:-}"
CLIENT_IP="${CLIENT_IP:-192.168.13.188/24}"
GATEWAY="${GATEWAY:-192.168.13.1}"
PORT="${PORT:-5004}"
AUTO_INSTALL="${AUTO_INSTALL:-1}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [[ -n "${RECV_SCRIPT:-}" ]]; then
  :
elif [[ -f "$SCRIPT_DIR/recv-rtp.py" ]]; then
  RECV_SCRIPT="$SCRIPT_DIR/recv-rtp.py"
elif [[ -f "$HOME/recv-rtp.py" ]]; then
  RECV_SCRIPT="$HOME/recv-rtp.py"
else
  RECV_SCRIPT=""
fi

GST_PKGS=(
  gstreamer1.0-tools
  gstreamer1.0-plugins-base
  gstreamer1.0-plugins-good
  gstreamer1.0-plugins-bad
  gstreamer1.0-plugins-ugly
  gstreamer1.0-libav
  gstreamer1.0-x
  python3-gi
  python3-gi-cairo
  gir1.2-gstreamer-1.0
)

need_install=0
if ! command -v gst-inspect-1.0 >/dev/null 2>&1; then
  need_install=1
elif ! gst-inspect-1.0 rtph264depay >/dev/null 2>&1; then
  need_install=1
elif ! gst-inspect-1.0 h264parse >/dev/null 2>&1; then
  need_install=1
fi

if [[ "$need_install" -eq 1 ]]; then
  echo "缺少 GStreamer RTP/H.264 解碼元件。"
  if [[ "$AUTO_INSTALL" != "1" ]]; then
    echo "請手動安裝：sudo apt-get install -y ${GST_PKGS[*]}"
    exit 1
  fi
  echo "自動安裝：${GST_PKGS[*]}"
  sudo apt-get update -qq
  sudo apt-get install -y "${GST_PKGS[@]}"
fi

if [[ -z "$IFACE" ]]; then
  IFACE=$(nmcli -t -f DEVICE,TYPE device status 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}' || true)
fi
if [[ -z "$IFACE" ]]; then
  echo "找不到 Wi-Fi 介面，請設定 IFACE=wlan0（或你的 wifi 介面名）"
  exit 1
fi

echo "=== Client 連線 OpenWiFi AP ==="
echo "iface=$IFACE ssid=$SSID ip=$CLIENT_IP"

if nmcli -t -f NAME connection show | grep -Fxq "$CONN_NAME"; then
  sudo nmcli connection modify "$CONN_NAME" \
    connection.interface-name "$IFACE" \
    802-11-wireless.ssid "$SSID" \
    802-11-wireless.mode infrastructure \
    ipv4.method manual \
    ipv4.addresses "$CLIENT_IP" \
    ipv4.gateway "$GATEWAY" \
    ipv4.dns "8.8.8.8" \
    ipv6.method ignore
else
  sudo nmcli connection add type wifi ifname "$IFACE" con-name "$CONN_NAME" ssid "$SSID" \
    ipv4.method manual \
    ipv4.addresses "$CLIENT_IP" \
    ipv4.gateway "$GATEWAY" \
    ipv4.dns "8.8.8.8" \
    ipv6.method ignore
fi

sudo nmcli connection up "$CONN_NAME" || {
  echo "連線失敗。請確認 OpenWiFi 已開 AP (SSID=$SSID)"
  nmcli device wifi list | head -20
  exit 1
}

echo "目前位址："
ip -br addr show "$IFACE" 2>/dev/null || ip addr show "$IFACE"
ping -c 2 -W 2 192.168.13.1 || echo "警告：ping 192.168.13.1 失敗（AP/DHCP 閘道）"
ping -c 2 -W 2 192.168.13.111 || echo "提示：192.168.13.111 需等 NS-3 啟動後才會回應"

if [[ -z "$RECV_SCRIPT" || ! -f "$RECV_SCRIPT" ]]; then
  echo "找不到 recv-rtp.py。"
  echo "請從 ns-3 專案複製：scratch/rtp-exp/recv-rtp.py"
  echo "或設定：RECV_SCRIPT=/path/to/recv-rtp.py $0"
  exit 1
fi

echo "=== 啟動 RTP 接收 (UDP $PORT) script=$RECV_SCRIPT ==="
exec python3 "$RECV_SCRIPT" --port "$PORT"
