# 實驗五：OpenWiFi 相容 Mesh Gateway 之跨層 QoS 與 RTP 轉發驗證

---

## 一、實驗前言

### 1.1 研究動機（因果鏈）

- **問題起點**：即時影音（RTP/H.264）與背景探測（ICMP ping、iperf）共用無線 mesh 時，封包在 MAC 層競爭信道，易造成 FPS 下降、抖動與吞吐不穩。
- **既有方案限制**：
  - 商用 AP / OpenWrt 可在閘道做 DiffServ 標記，但 MAC/PHY 參數調整彈性有限。
  - **openwifi** 可細調 MAC/PHY，但 FPGA 閘道本身**不解析 IP 以上欄位**，難以依應用類型分類。
  - 既有 **NS-3 + openwifi（FdNetDevice）** 架構已能模擬 L3 轉發（如 ping transit、reverse NAT），但**未內建依 L4/承載格式做流量辨識與跨層 QoS 映射**。
- **本研究切入**：在雙閘道 mesh 模擬架構中，於**入口閘道路由模組**完成 L4 分類與 IP TOS 標記，並在 mesh Wi-Fi MAC 透過 **EDCA 參數調整 + NS-3 DS→AC 映射**，使 RTP 與背景流在無線段獲得差異化服務。
- **驗證目標**：先證明「可觀測的端到端 RTP 轉發」，再比較「啟用/未啟用 QoS 及背景干擾」對接收端 FPS/吞吐的影響。

### 1.2 文獻佐證需求（建議引用）

| 論述 | 需佐證之處 |
|------|-----------|
| QoS 對即時影音之必要性 | ITU-T G.114（延遲）、RFC 3550（RTP）、IEEE 802.11e / WMM（EDCA） |
| openwifi 能力與限制 | openwifi 專案論文 / 技術報告（MAC/PHY 可編程、閘道 L2 轉發） |
| NS-3 與實機橋接 | NS-3 FdNetDevice / emu 文件；既有 mesh-emu-openwifi 或同類研究 |
| DiffServ 標記語意 | RFC 2474/3246（DSCP/EF/CS）；本實驗 RTP 預設 `TOS=0xB8`（EF→AC_VI） |
| 實驗場景與背景流設計 | sconn 等相關論文（mesh 場景、ping 背景流、距離配置） |
| H.264 over RTP 測試合理性 | RFC 6184（H.264 RTP payload）、GStreamer 實務慣例 |

### 1.3 前言審查：需修正之表述

| 原表述 | 問題 | 建議改寫 |
|--------|------|----------|
| 「讓 OpenWiFi 設備擁有**應用層**封包解析」 | 程式僅做到 **L4（UDP port）+ RTP 承載頭** 解析，未解析 SSH/HTTP 等應用層語意 | 改為「**傳輸層/ RTP 承載層**流量辨識與統計」 |
| 「本文提出一套已部署於 OpenWiFi 的系統」 | 本實驗為 **NS-3 模擬 + FdNetDevice 橋接實機網段**，非直接在 Zedboard 跑新韌體 | 改為「**相容 openwifi+NS-3 架構**之閘道設計，並以 emu 驗證」 |
| 「IP 標記可自主驅動 Linux 在 MAC 打 QoS 印記」 | 實作為 **NS-3 入口改寫 IPv4 TOS**，再由 **WifiNetDevice::SelectQueue** 做 DS→AC；非直接操作 Linux `tc`/`nl80211` | 改為「**L3 標記 → 802.11e AC 映射**（模擬層與 openwifi 部署路徑需分開描述）」 |
| 「RTP 標記 EF 後進入 AC_VO」 | `0xB8`（EF）在 NS-3 映射為 **AC_VI（UP=5）**，非 AC_VO | 對照 NS-3 wifi-user 文件之 TOS→AC 表 |
| 「實驗 5-1 驗證 OpenWiFi 能轉發 L3 以上封包」 | 5-1 實際驗證的是 **emu 閘道 NAT 轉發 + 四點 RTP 統計一致性**，非 OpenWiFi 硬體本體 | 對齊實際量測點敘述 |
| 「未來毫米波 Gateway」 | 屬展望，與本實驗無直接因果 | 移至結論/未來工作，不寫入實驗假設 |

### 1.4 研究假設（可檢驗）

- H1：雙閘道 NAT 轉發路徑可穩定傳遞 RTP（sender → ingress → mesh → egress → receiver 四點 FPS/吞吐應趨近）。
- H2：在背景 ping/iperf 干擾下，啟用 `--enable-qos=1` 後，接收端 RTP FPS/吞吐下降幅度**小於**未啟用 QoS。
- H3：入口/出口閘道之 `RtpFpsStats` 與實機 sender/receiver 統計，可用於定位瓶頸區段（mesh 內 vs 有線側）。

---

## 二、研究方法（依程式架構抽象）

### 2.1 整體架構

- **模擬核心**：NS-3 `mesh-emu-iperf-rtp`（延伸 `mesh-emu-m13`）。
- **拓撲抽象**：`4×3` 網格共 12 節點，IEEE 802.11s HWMP mesh（`10.1.1.0/24`），節點間距預設 **30 m**。
- **雙邊界閘道**：
  - **Ingress（node 0）**：`FdNetDevice` 橋接實機 `ens33`，閘道 IP `192.168.4.111`。
  - **Egress（node 9）**：`FdNetDevice` 橋接實機 `ens39`，閘道 IP `192.168.1.111`。
- **實機參與者**：
  - 傳輸端：`192.168.4.x`（GStreamer 送 H.264/RTP）。
  - 接收端：`192.168.1.188`（GStreamer 收流統計）。
  - 背景流：外部 `ping` / 可選 `iperf3`。

### 2.2 閘道路由模組（`MeshBorderNatRouting`）

- **Ingress 處理（outside Rx）**：
  - **DNAT**：目的為 `ingress-addr:port` 時，依埠轉發至 `192.168.1.188`（RTP `5004`、iperf `5201`）。
  - **Transit**：目的為 `192.168.1.0/24` 時直接轉入 mesh（不改目的位址）。
  - **QoS 分類（`ApplyIngressQos`）**：依 L4 辨識後改寫 IPv4 TOS（RTP/ICMP/其他）。
  - **Reverse NAT**：回程至 `ingress-addr` 之 TCP/UDP 還原為真實 client IP。
- **Egress 處理（outside Tx）**：
  - **SNAT**：mesh/外網來源改寫為 `egress-addr`（`192.168.1.111`）。
  - **L4 checksum 修補**：TCP/UDP 在 NAT 後重算校驗和。
  - **IP 分片 SNAT**：大封包（如 iperf UDP）之後續 fragment 同步改寫來源 IP。
- **ICMP**：echo 轉發、echo-reply 反向 DNAT/SNAT（與 openwifi 版邏輯對齊）。

### 2.3 流量辨識與統計（`RtpFpsStats`）

- **觸發點**：ingress（node 0）與 egress（node 9）對 **UDP dst port = rtp-port** 之封包。
- **解析深度**：UDP 負載之 **RTP 固定頭**（version、PT、seq、timestamp、marker）+ **H.264 NAL**（IDR/I-frame 偵測）。
- **輸出指標**（每 `rtp-stats-interval` 秒）：`fps`、`throughput_kbps`、`frames`、`idr_frames`、`rtp_packets`、`bytes`。
- **輸出檔**：`node0-rtp-stats.csv`、`node9-rtp-stats.csv`。
- **實機對照**：`send-rtp.py` / `recv-rtp.py` 以 GStreamer probe 產生 `send-rtp-stats.csv`、`recv-rtp-stats.csv`。

### 2.4 QoS 實作（`--enable-qos`）

- **L3 分類標記（入口閘道）**：
  - RTP（UDP dport `5004`）→ `rtp-tos` 預設 `0xB8`（EF）。
  - ICMP → `icmp-tos` 預設 `0x08`（CS1）。
  - 其他 → `default-tos` 預設 `0x00`（BE）。
- **L2 調度（mesh 全節點 `ConfigureMeshEdca`）**：分別設定 AC_VO / AC_VI / AC_BE / AC_BK 之 AIFSN、CWmin、CWmax。
- **跨層映射機制**：NS-3 依 IP DS field → User Priority → Access Category（**EF 對應 AC_VI**）；非「Linux tc 直接寫 MAC queue」。

### 2.5 實驗控制變項

| 變項 | 程式參數 | 預設 |
|------|----------|------|
| QoS 開關 | `--enable-qos` | `0`（關） |
| RTP 目的 | `--rtp-remote` | `192.168.1.188` |
| 統計週期 | `--rtp-stats-interval` | `1.0` s |
| 模擬時間 | `--time` | `100` s |
| 背景 ping（mesh 內） | `--enable-ping=1` | 關 |
| 背景 ping（實機） | `ping -i 0.001 192.168.1.188` | 實驗場景指定 |

---

## 三、實驗設計

### 3.1 實驗 5-1：端到端 RTP 轉發可觀測性（Exp1）

- **目的**：驗證 NAT transit 路徑下，RTP 可由外網段送達接收端，且四個量測點數值一致。
- **自變項**：無（基線）。
- **依變項**：FPS、throughput（kbps）、frames/rtp_packets。
- **量測點**：sender → node0（ingress）→ node9（egress）→ receiver。
- **啟動**：
  - NS-3：`./ns3 run mesh-emu-iperf-rtp -- --enable-ping=0 --enable-qos=0`
  - 傳輸端：`send-rtp.py`（或 gst 送 `192.168.4.111:5004`）
  - 接收端：`recv-rtp.py`（`192.168.1.188:5004`）
- **分析**：`Exp1/plot-exp.py` 產生 `compare-fps.png`、`compare-throughput.png`（橫軸預設 **Sample #**，避免 sender 牆鐘與模擬時間錯位）。

### 3.2 實驗 5-2：QoS 與背景干擾（Exp2）

- **目的**：驗證入口 L4 分類 + TOS 標記 + mesh EDCA 調整，能否在干擾下改善接收端 RTP 品質。
- **場景因子**（對應 `Exp2/compare-exp.py`）：
  - **noIntf**：無額外干擾（基線）。
  - **noQoS**：有背景流（ping/iperf），`--enable-qos=0`。
  - **qos**：有背景流，`--enable-qos=1`。
- **依變項**：接收端 `recv-rtp-stats.csv` 之 FPS / throughput（可選 jitter、lost 若擴充統計）。
- **控制**：相同影片源、相同 `rtp-stats-interval`、相同模擬時長與節點佈局。

### 3.3 實驗場景（物理/網路）

- **不與商用 AP 比較**：聚焦 openwifi 相容架構在運算/射頻受限平台之可行性，而非絕對效能標竿。
- **拓撲參考**：sconn 論文式 mesh 場景——主流量為即時影音，輔以 ping 維持背景負載。
- **節點距離**：網格 `step=30 m`（程式可調 `--step`）；需在報告中記載實際部署是否與模擬一致。
- **選用 RTP 之理由**：
  - 具明確即時指標（FPS、seq gap、IDR 率）。
  - 程式已實作 RTP 承載解析，可同時做轉發與被動量測。
  - 與實驗室既有 GStreamer 工具鏈一致。
- **Ping 背景流**：
  - 實機：`sudo ping -i 0.001 192.168.1.188`（高頻 ICMP，經 ingress transit → mesh → egress SNAT）。
  - 模擬內（可選）：`--enable-ping=1 --ping-node=8`（mesh 節點對 `ping-remote` 發送，`IpTos` 隨 QoS 設定）。
- **操作限制**：NS-3 閘道 PC 不可用自身 `192.168.4.x` 當 client（FdNetDevice hairpin 限制）；傳輸端須為**另一台** `192.168.4.x` 主機。

---

## 四、實驗結果設置與描述

### 4.1 Exp1：四點轉發一致性（呼應 H1）

- **圖表設置**：
  - `compare-fps.png`：四節點 FPS 隨 sample 變化。
  - `compare-throughput.png`：四節點吞吐（kbps）隨 sample 變化。
- **描述要點**（撰寫時對照 CSV 填數）：
  - sender 與 node0 平均吞吐應接近（ingress 量測為 UDP 封包級，sender 為 GStreamer 輸出級）。
  - node0 → node9 → receiver 若遞減，表示 mesh 或多跳段為主要損失點。
  - FPS 曲線若同步起伏，代表為網路/編碼共同效應；若僅 receiver 異常，檢查 NAT、checksum 或接收端解碼。
- **判定標準**：四點平均 FPS/吞吐差異在可接受門檻內（建議報告定義 ±X%），且無持續性 seq gap 飆升。

### 4.2 Exp2：QoS 效益比較（呼應 H2）

- **圖表設置**（`Exp2/compare-exp.py` 產出，建議拆為四子圖）：
  - FPS：`noIntf` vs `noQoS` vs `qos`。
  - Throughput：同上三場景。
- **描述要點**：
  - **noIntf**：理論上為接收端上限基線。
  - **noQoS + 背景流**：預期 FPS/吞吐下降（ICMP/iperf 與 RTP 同競爭 mesh）。
  - **qos**：若 L3 標記 + EDCA 生效，接收端下降幅度應小於 noQoS；需同時報告平均與最低 FPS。
- **必要說明**：QoS 獲益取決於背景流強度（`ping -i`、iperf `-b`）；單次結果不足以推論所有負載條件。

### 4.3 結果詮釋限制（呼應前言審查）

- 結果驗證的是 **NS-3 emu 架構**，需後續移植至 openwifi 實機方能宣稱「OpenWiFi 設備已實現」。
- 統計為 **RTP 層可觀測指標**，非完整 MOS/VMOS 影音品質主觀評分。
- EF→AC_VI 映射下，QoS 效益來自「相對降級背景流」而非保證進入 AC_VO；若需最高優先，應調整 `rtp-tos`（如 CS6/`0xC0`→AC_VO）並重跑對照。

### 4.4 圖表占位

**Exp1**

![Exp1 FPS 四點比較](Exp1/compare-fps.png)

![Exp1 Throughput 四點比較](Exp1/compare-throughput.png)

**Exp2**（待填入產出檔名）

- FPS 比較：<!-- recv-rtp-stats-noIntf / noQoS / qos -->
- Throughput 比較：<!-- 同上 -->

---

## 五、撰寫備忘

- 標題「名初」建議改為具體研究名稱（如「基於 NS-3 emu 之 Mesh 閘道跨層 QoS 驗證」）。
- 每張圖需附：場景參數（`enable-qos`、背景流指令）、樣本數、暖機裁剪方式（`--warmup`）。
- 因果用語：用「在…條件下觀察到…」；避免「證明 OpenWiFi 已具備應用層解析」等過度推論。
