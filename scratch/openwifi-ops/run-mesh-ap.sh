#!/bin/bash
# OpenWiFi 板端：載入 SDR → 開 AP (SSID=openwifi) → 跑 mesh-emu
# 依板子 history: ./wgd.sh → ./fosdem.sh → ip link set promisc → ./ns3.37-mesh-emu-*-debug
set -euo pipefail

SCRATCH_DIR="${SCRATCH_DIR:-$HOME/ns-3/scratch}"
OPENWIFI_DIR="${OPENWIFI_DIR:-$HOME/openwifi}"
# 優先新交叉編譯產物；否則回退既有 mesh-emu-m13
if [[ -x "$SCRATCH_DIR/ns3.37-mesh-emu-m13-OpenWifi-debug" ]]; then
  BIN="${BIN:-$SCRATCH_DIR/ns3.37-mesh-emu-m13-OpenWifi-debug}"
else
  BIN="${BIN:-$SCRATCH_DIR/ns3.37-mesh-emu-m13-debug}"
fi
HOSTAPD_CONF="${HOSTAPD_CONF:-$OPENWIFI_DIR/hostapd-openwifi.conf}"
SSID="${SSID:-openwifi}"
INGRESS_DEV="${INGRESS_DEV:-br0}"
EGRESS_DEV="${EGRESS_DEV:-sdr0}"
RTP_REMOTE="${RTP_REMOTE:-192.168.13.188}"
RTP_PORT="${RTP_PORT:-5004}"
SIM_TIME="${SIM_TIME:-100}"
ENABLE_QOS="${ENABLE_QOS:-0}"

cd "$SCRATCH_DIR"

if [[ ! -x "$BIN" ]]; then
  echo "找不到可執行檔: $BIN"
  echo "請先在 .226 交叉編譯並 scp 到 $SCRATCH_DIR"
  exit 1
fi

echo "=== OpenWiFi AP + mesh-emu ==="
echo "bin=$BIN"
echo "ingress=$INGRESS_DEV  egress=$EGRESS_DEV  rtp-remote=$RTP_REMOTE:$RTP_PORT"

# --- 1) 載入 FPGA/driver（若尚無 sdr0）---
if ! ip link show "$EGRESS_DEV" >/dev/null 2>&1; then
  echo "找不到 $EGRESS_DEV，執行 $OPENWIFI_DIR/wgd.sh ..."
  if [[ ! -x "$OPENWIFI_DIR/wgd.sh" ]]; then
    echo "缺少 $OPENWIFI_DIR/wgd.sh"
    exit 1
  fi
  (cd "$OPENWIFI_DIR" && ./wgd.sh)
  sleep 2
fi
if ! ip link show "$EGRESS_DEV" >/dev/null 2>&1; then
  echo "wgd.sh 後仍無 $EGRESS_DEV"
  ip link
  exit 1
fi

# --- 2) AP mode（fosdem.sh 風格）---
killall hostapd 2>/dev/null || true
sleep 1

if [[ ! -f "$HOSTAPD_CONF" ]]; then
  echo "找不到 $HOSTAPD_CONF"
  exit 1
fi

ip link set "$EGRESS_DEV" up || true
# Linux AP / DHCP 閘道；NS-3 SNAT 用 192.168.13.111
ip addr flush dev "$EGRESS_DEV" 2>/dev/null || true
ip addr add 192.168.13.1/24 dev "$EGRESS_DEV" || true

rm -f /var/run/dhcpd.pid 2>/dev/null || true
service isc-dhcp-server restart 2>/dev/null || true

echo "啟動 hostapd ($HOSTAPD_CONF, SSID=$SSID) ..."
hostapd "$HOSTAPD_CONF" >/tmp/hostapd-m13.log 2>&1 &
HOSTAPD_PID=$!
sleep 3
if ! kill -0 "$HOSTAPD_PID" 2>/dev/null; then
  echo "hostapd 啟動失敗，見 /tmp/hostapd-m13.log"
  cat /tmp/hostapd-m13.log || true
  exit 1
fi
echo "hostapd PID=$HOSTAPD_PID"

# 預設路由回編譯/發送 PC
ip route replace default via 192.168.10.1 2>/dev/null || true

# --- 3) FdNetDevice 需要 promisc（history 亦如此）---
ip link set "$INGRESS_DEV" up || true
ip link set "$INGRESS_DEV" promisc on
ip link set "$EGRESS_DEV" promisc on

EXTRA_ARGS=()
if [[ "$ENABLE_QOS" == "1" ]]; then
  EXTRA_ARGS+=(--enable-qos=1)
fi

echo "=== 啟動 NS-3 ==="
CMD=(
  "$BIN"
  --ingress-device="$INGRESS_DEV"
  --egress-device="$EGRESS_DEV"
  --ingress-addr=192.168.10.111
  --ingress-gateway=192.168.10.1
  --egress-addr=192.168.13.111
  --egress-gateway=192.168.13.1
  --rtp-remote="$RTP_REMOTE"
  --rtp-port="$RTP_PORT"
  --time="$SIM_TIME"
  --enable-ping=0
  --pcap=0
)
if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
  CMD+=("${EXTRA_ARGS[@]}")
fi
printf 'run:'; printf ' %q' "${CMD[@]}"; echo
exec "${CMD[@]}"
