# OpenWiFi + NS-3 Mesh RTP 實驗手冊

本目錄提供把 **NS-3 mesh gateway** 跑在 **OpenWiFi** 板上，並以 **H.264/RTP** 做端到端影音驗證的腳本與步驟。

適用對象：已 clone 本 repo（或整包 ns-3.37 專案）的使用者。腳本會依自身位置推導 `NS3_ROOT`，**不依賴固定安裝路徑**。

```bash
# 以下一律假設你在 ns-3 根目錄（含 ./ns3、scratch/ 的那層）
cd /path/to/your/ns-3.37          # ← 改成你的實際路徑
export NS3_ROOT="$PWD"            # 可選；不設也可，腳本會自動推導
```

---

## 架構一覽

```
[Sender PC]  --UDP RTP-->  192.168.10.111 (br0, NS-3 node0 DNAT)
                                |
                           ns-3 mesh 10.1.1.0/24
                                |
                             192.168.13.111 (sdr0, NS-3 node9 SNAT)
                                |
                          SSID openwifi / 192.168.13.1 (hostapd + DHCP)
                                |
                          [Client STA = 192.168.13.188]  recv-rtp.py :5004
```

### 預設角色與位址（可用環境變數覆寫）

| 角色 | 預設位址 / 帳密 | 說明 |
|------|-----------------|------|
| 編譯 + 發送端 PC | `192.168.10.1`（與板子同網段） | 交叉編譯 ARM binary、送 RTP |
| OpenWiFi 板 | `192.168.10.122` / `root` / `openwifi` | 跑 NS-3 + `sdr0` AP |
| NS-3 ingress | `192.168.10.111` @ `br0` | FdNetDevice 入口 |
| NS-3 egress / SNAT | `192.168.13.111` @ `sdr0` | 出無線網段 |
| AP / DHCP gateway | `192.168.13.1` @ `sdr0` | hostapd SSID=`openwifi` |
| 接收端 Client | `192.168.13.188` | 連 AP 後收 RTP |

若你的實驗網段不同，請改腳本環境變數（見各節），不必改死程式路徑。

### 本 repo 相關檔案

| 路徑 | 用途 |
|------|------|
| `scratch/mesh-emu-m13-OpenWifi.cc` | NS-3 主程式（預設 `br0`/`sdr0`） |
| `scratch/Rtp/` | `rtp-helper.h` 等（由 `scratch/CMakeLists.txt` 連結） |
| `scratch/rtp-exp/send-rtp.py` | 發送端統計腳本 |
| `scratch/rtp-exp/recv-rtp.py` | 接收端腳本（請拷到 Client） |
| `scratch/rtp-exp/naruto.h264` | 範例影片（若 repo 未含大檔，請自行放置） |
| `scratch/openwifi-ops/*.sh` | 本手冊對應操作腳本 |

---

## 快速流程（三機）

1. **Sender PC**：交叉編譯並上傳 → 板上開 AP+NS-3 → Client 連線收流 → Sender 發 RTP  
2. 建議啟動順序：`run-mesh-ap.sh` → `connect-client.sh` → `send-from-226.sh`  
3. 結束：各端 Ctrl+C；板上可 `killall hostapd`

---

## 0. 前置需求

### 0.1 Sender PC（編譯機 / 發影片）

- Linux（Ubuntu 建議）、已能建置本 repo 的 ns-3
- 與 OpenWiFi 有線同網段（預設 `192.168.10.0/24`）
- ARM 交叉編譯器：

```bash
sudo apt-get install -y g++-8-arm-linux-gnueabi gcc-8-arm-linux-gnueabi
sudo update-alternatives --install \
  /usr/bin/arm-linux-gnueabi-g++ arm-linux-gnueabi-g++ /usr/bin/arm-linux-gnueabi-g++-8 10
sudo update-alternatives --install \
  /usr/bin/arm-linux-gnueabi-gcc arm-linux-gnueabi-gcc /usr/bin/arm-linux-gnueabi-gcc-8 10
arm-linux-gnueabi-g++ --version
```

（若發行版套件名稱不同，安裝任何可產生 `arm-linux-gnueabi-g++` 的工具鏈即可。）

GStreamer（`send-from-226.sh` 缺套件時會自動 `apt install`；也可手動）：

```bash
sudo apt-get install -y \
  gstreamer1.0-tools gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
  gstreamer1.0-plugins-ugly gstreamer1.0-libav \
  python3-gi python3-gi-cairo gir1.2-gstreamer-1.0
```

### 0.2 OpenWiFi 板

- 已能 SSH：`ssh root@192.168.10.122`（密碼預設 `openwifi`）
- 已有 `~/openwifi/`（含 `wgd.sh`、`hostapd-openwifi.conf`、`fosdem.sh`）
- 建議有 `br0` 橋接 `eth0`（本實驗 ingress 預設 `br0`）
- 建議有 `isc-dhcp-server`，subnet `192.168.13.0/24`，router `192.168.13.1`
- 板上放置 binary 的目錄預設：`~/ns-3/scratch/`

若 **ping 通但 SSH 被關掉 / timeout**：重開板子後再試（sshd 偶發卡住）。

### 0.3 Client PC（收影片）

- 有 Wi-Fi、可用 `nmcli`
- GStreamer + Python GI（`connect-client.sh` 也會自動安裝）
- 從本 repo 複製接收腳本：

```bash
# 在 Sender / 有 repo 的機器上：
scp "$NS3_ROOT/scratch/openwifi-ops/connect-client.sh" \
    "$NS3_ROOT/scratch/rtp-exp/recv-rtp.py" \
    <user>@<client-host>:~/
```

---

## 1. 交叉編譯並上傳到 OpenWiFi

在 **Sender PC** 的 ns-3 根目錄：

```bash
cd "$NS3_ROOT"    # 或 cd /path/to/your/ns-3.37
chmod +x scratch/openwifi-ops/*.sh
./scratch/openwifi-ops/cross-build.sh
```

腳本會：

1. `./ns3 clean` + `CXX=arm-linux-gnueabi-g++` 靜態 debug configure  
2. `./ns3 build -j 1`（記憶體不足會重試，預設最多 5 次）  
3. `scp` binary 與 `run-mesh-ap.sh` 到板上 `~/ns-3/scratch/`

常用覆寫：

```bash
OPENWIFI_HOST=root@192.168.10.122 \
REMOTE_DIR='~/ns-3/scratch' \
MAX_TRIES=8 \
./scratch/openwifi-ops/cross-build.sh
```

### 手動等價指令

```bash
cd "$NS3_ROOT"
./ns3 clean
CXX="arm-linux-gnueabi-g++" CXXFLAGS_EXTRA="-march=arm" \
  ./ns3 configure --enable-static --build-profile=debug

# 板子 RAM 小，務必 -j 1；失敗就再跑
./ns3 build -j 1

file build/scratch/ns3.37-mesh-emu-m13-OpenWifi-debug
# 應類似: ELF 32-bit LSB executable, ARM, ... statically linked

scp build/scratch/ns3.37-mesh-emu-m13-OpenWifi-debug \
    scratch/openwifi-ops/run-mesh-ap.sh \
    root@192.168.10.122:~/ns-3/scratch/
```

`scratch/CMakeLists.txt` 已將 `mesh-emu-m13-OpenWifi.cc` 與 `scratch/Rtp/*.cc` 一起編譯，並加入 `Rtp` include path，因此 `#include "rtp-helper.h"` 會從 `scratch/Rtp/` 解析。

---

## 2. 在 OpenWiFi 開 AP 並跑 NS-3

```bash
ssh root@192.168.10.122
cd ~/ns-3/scratch
chmod +x ns3.37-mesh-emu-m13-OpenWifi-debug run-mesh-ap.sh
./run-mesh-ap.sh
```

腳本行為（對齊常見板上流程：`wgd.sh` → hostapd → promisc → mesh binary）：

1. 若無 `sdr0`，執行 `~/openwifi/wgd.sh` 載入 FPGA/driver  
2. `hostapd ~/openwifi/hostapd-openwifi.conf`（SSID=`openwifi`）  
3. `sdr0` = `192.168.13.1`，重啟 DHCP  
4. `br0` / `sdr0` 開 promiscuous（FdNetDevice 需要）  
5. 執行 NS-3：ingress=`br0`、egress=`sdr0`、RTP 目的預設 `192.168.13.188:5004`

環境變數範例：

```bash
RTP_REMOTE=192.168.13.188 SIM_TIME=120 ENABLE_QOS=1 ./run-mesh-ap.sh
INGRESS_DEV=eth0 ./run-mesh-ap.sh          # 若沒有 br0
BIN=/root/ns-3/scratch/ns3.37-mesh-emu-m13-OpenWifi-debug ./run-mesh-ap.sh
```

預期日誌含：`[node0-br0 RTP ingress]`、`[node9-sdr0 ...]`。

若沒有新的 `ns3.37-mesh-emu-m13-OpenWifi-debug`，腳本會回退 `ns3.37-mesh-emu-m13-debug`。

---

## 3. Client 連 AP 並收 RTP

在 Client：

```bash
chmod +x ~/connect-client.sh
# 同目錄需有 recv-rtp.py（見 0.3）
./connect-client.sh
```

腳本會：

- 自動安裝缺的 GStreamer 套件  
- 用 `nmcli` 連 SSID `openwifi`  
- 固定 IP `192.168.13.188/24`、gateway `192.168.13.1`  
- 執行 `python3 recv-rtp.py --port 5004`

覆寫範例：

```bash
IFACE=wlan0 CLIENT_IP=192.168.13.188/24 SSID=openwifi ./connect-client.sh
RECV_SCRIPT=/opt/rtp/recv-rtp.py ./connect-client.sh
```

請確認 Client 位址為 **`192.168.13.188`**（與板上 `--rtp-remote` 一致）。

---

## 4. Sender 發送 RTP 影片

等板上 NS-3 與 Client 接收都就緒後，在 **Sender PC**：

```bash
cd "$NS3_ROOT"
./scratch/openwifi-ops/send-from-226.sh
```

腳本會：

- 檢查並安裝 GStreamer（含 `h264parse` / `gstreamer1.0-plugins-bad`）  
- 自動選擇通往 `192.168.10.111` 的網卡  
- 優先送 `scratch/rtp-exp/naruto.h264`；若無則嘗試 `naruto.mp4`

覆寫範例：

```bash
HOST=192.168.10.111 PORT=5004 IFACE=eth0 \
FILE=/path/to/video.h264 \
./scratch/openwifi-ops/send-from-226.sh
```

手動等價：

```bash
cd "$NS3_ROOT/scratch/rtp-exp"
python3 send-rtp.py --host 192.168.10.111 --port 5004 naruto.h264
```

或：

```bash
gst-launch-1.0 filesrc location=naruto.h264 ! h264parse ! \
  rtph264pay config-interval=1 pt=96 ! \
  udpsink host=192.168.10.111 port=5004 sync=true
```

若封包進不了 emu，依 NS-3 印出的 emu MAC：

```bash
sudo ip neigh replace 192.168.10.111 lladdr <EMU_MAC> dev <你的網卡>
```

注意：FdNetDevice hairpin 限制下，不要用「同一台機器既當 gateway PC 又從本機位址測回程 ping」來判斷通斷；本實驗發送端應走 `192.168.10.1` → 板上 `192.168.10.111`。

---

## 5. 建議啟動順序與檢查點

| 順序 | 機器 | 指令 | 檢查 |
|------|------|------|------|
| 1 | OpenWiFi | `./run-mesh-ap.sh` | hostapd 起來；有 `sdr0`；NS-3 印出 ingress/egress |
| 2 | Client | `./connect-client.sh` | Wi-Fi 連上 `openwifi`；IP=`192.168.13.188` |
| 3 | Sender | `./scratch/openwifi-ops/send-from-226.sh` | 有 FPS/吞吐；板上有 RTP ingress log |
| 4 | Client | （已在收） | 視窗出畫 / ASCII FPS |

結束：Ctrl+C；板上 `killall hostapd`。

---

## 6. 環境變數速查

| 變數 | 預設 | 用在 |
|------|------|------|
| `NS3_ROOT` | 由腳本路徑推導 | 所有 Sender 腳本 |
| `OPENWIFI_HOST` | `root@192.168.10.122` | `cross-build.sh` |
| `REMOTE_DIR` | `~/ns-3/scratch` | `cross-build.sh` |
| `HOST` / `PORT` | `192.168.10.111` / `5004` | `send-from-226.sh` |
| `IFACE` | 自動偵測 | 發送 / Client |
| `FILE` | `$NS3_ROOT/scratch/rtp-exp/naruto.h264` | `send-from-226.sh` |
| `RTP_REMOTE` | `192.168.13.188` | `run-mesh-ap.sh` |
| `INGRESS_DEV` / `EGRESS_DEV` | `br0` / `sdr0` | `run-mesh-ap.sh` |
| `ENABLE_QOS` | `0` | `run-mesh-ap.sh` |
| `AUTO_INSTALL` | `1` | 發送 / Client（設 `0` 可關閉自動 apt） |

---

## 7. 常見問題

| 現象 | 處理 |
|------|------|
| `no element "h264parse"` | 缺 `gstreamer1.0-plugins-bad`；重跑發送腳本或手動 apt |
| `./ns3 build -j 1` 被 kill | 記憶體不足，多試幾次；關閉其他大程式 |
| 編譯出 x86 而非 ARM | configure 時必須設 `CXX=arm-linux-gnueabi-g++`，用 `file` 檢查產物 |
| `#include "rtp-helper.h"` 找不到 | 使用本 repo 的 `scratch/CMakeLists.txt`（含 Rtp include） |
| Client 連不上 `openwifi` | 板上 `ps \| grep hostapd`；距離/頻道；先確認 `wgd.sh` 成功 |
| 有連線無影像 | Client 是否為 `192.168.13.188`；`--rtp-remote`；UDP `5004` |
| SSH 連不上板子 | ping 若通則重開板；避免短時間大量連線觸發 sshd 拒絕 |
| 找不到 `naruto.h264` | 自行放入 `scratch/rtp-exp/`，或用 `FILE=` 指定其他 H.264 |

---

## 8. 授權與硬體前提

- 本流程假設你已有可運作的 **OpenWiFi** 影像與 user-space（`~/openwifi`），本 repo 不包含 FPGA bitstream 的完整燒錄教學。  
- ns-3 本體請遵循上游授權；本目錄腳本僅為實驗操作輔助。
