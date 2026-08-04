#!/usr/bin/env python3
import gi
import time
import threading
import sys
import shutil
import math
from collections import deque

gi.require_version("Gst", "1.0")
gi.require_version("GObject", "2.0")
from gi.repository import Gst, GObject, GLib

# -------------------------
# 初始化
# -------------------------
Gst.init(None)

# 全域變數
TOTAL_PACKETS = 0 
MAX_PEAK_ALL_TIME = 0 # 紀錄有史以來的最高峰值 (僅供參考)

# 設定圖表高度 (行數) 與 歷史長度
GRAPH_HEIGHT = 10
HISTORY_LEN = 40  # X軸長度 (40秒)

# -------------------------
# ASCII XY軸繪圖函式 (修正版)
# -------------------------
def draw_ascii_graph(data_deque):
    global MAX_PEAK_ALL_TIME
    
    # --- 1. 動態刻度計算 (Auto-Scaling) ---
    
    # 找出目前視窗內的最大值
    if len(data_deque) > 0:
        current_window_max = max(data_deque)
    else:
        current_window_max = 0

    # 更新歷史最高紀錄
    if current_window_max > MAX_PEAK_ALL_TIME:
        MAX_PEAK_ALL_TIME = current_window_max

    # 設定 Y 軸的顯示上限 (Scale)
    # 取 "目前視窗最大值" 和 "10" 之間的較大者
    # (設底限 10 是為了避免數值為 0 或極小時圖表壞掉)
    display_max = max(current_window_max, 10)

    # --- 2. 準備畫布 ---
    # \033[F 上移一行, \033[K 清除該行
    sys.stdout.write(f"\033[{GRAPH_HEIGHT + 4}F") 

    # --- 3. 繪製標題 ---
    current_val = data_deque[-1] if len(data_deque) > 0 else 0
    # 顯示目前數值，以及目前的 Y 軸刻度上限
    print(f"\033[KStats: Current: \033[1;32m{current_val} FPS\033[0m | Peak(All-time): {MAX_PEAK_ALL_TIME} | Scale: 0-{display_max}")
    print(f"\033[K{'-'*60}")

    # --- 4. 繪製 Y 軸與柱狀體 ---
    for row in range(GRAPH_HEIGHT, 0, -1):
        line = ""
        
        # 計算這一行代表的數值門檻
        # 例如: 高度10行，Max是100，那第1行代表10，第5行代表50
        threshold = (row / GRAPH_HEIGHT) * display_max
        
        # Y軸標籤 (只在特定行數顯示，保持乾淨)
        if row == GRAPH_HEIGHT:
            line += f"{int(display_max):>4} ┤" # 頂端刻度
        elif row == GRAPH_HEIGHT // 2:
            line += f"{int(display_max/2):>4} ┤" # 中間刻度
        else:
            line += "     │" # 空白軸線

        # 繪製 X 軸數據
        for val in data_deque:
            if val >= threshold:
                # 視覺優化：如果數值遠大於門檻，用實心塊；剛好超過一點點，用半塊
                # 這可以讓波形看起來更平滑
                if val >= threshold + (display_max/GRAPH_HEIGHT)*0.5:
                    line += "█" 
                else:
                    line += "▄" 
            else:
                line += " " 
        
        print(f"\033[K{line}")

    # --- 5. 繪製底部 X 軸 ---
    print(f"\033[K     └" + ("─" * len(data_deque)))
    print(f"\033[K      (History: {len(data_deque)}s) \r") 
    
    sys.stdout.flush()

# -------------------------
# 統計監控執行緒
# -------------------------
def statistics_loop():
    last_total = 0
    
    # 初始化歷史數據
    DiffHistory = deque([0] * HISTORY_LEN, maxlen=HISTORY_LEN)
    
    # 預先印出空行佔位
    print("\n" * (GRAPH_HEIGHT + 5))

    while True:
        time.sleep(1) # 取樣頻率 1秒
        
        current_total = TOTAL_PACKETS
        diff = current_total - last_total
        
        DiffHistory.append(diff)
        
        draw_ascii_graph(DiffHistory)
        
        last_total = current_total

# 啟動統計線程
stat_thread = threading.Thread(target=statistics_loop, daemon=True)
stat_thread.start()

# -------------------------
# GStreamer Probe
# -------------------------
def on_rtp_probe(pad, info): 
    global TOTAL_PACKETS
    buffer = info.get_buffer()
    if buffer is None: return Gst.PadProbeReturn.OK
    TOTAL_PACKETS += 1
    return Gst.PadProbeReturn.OK

# -------------------------
# Pipeline
# -------------------------
pipeline = Gst.parse_launch(
    "udpsrc name=src port=5004 caps=application/x-rtp,encoding-name=H264 ! "
    "rtpjitterbuffer latency=0 ! rtph264depay ! h264parse ! decodebin ! videoconvert ! autovideosink sync=false"
)

src = pipeline.get_by_name("src")
pad = src.get_static_pad("src")
pad.add_probe(Gst.PadProbeType.BUFFER, on_rtp_probe)

# -------------------------
# Main Loop
# -------------------------
print("GStreamer Started. Waiting for data...")
pipeline.set_state(Gst.State.PLAYING)

loop = GLib.MainLoop()
try:
    loop.run()
except KeyboardInterrupt:
    pass
finally:
    pipeline.set_state(Gst.State.NULL)

