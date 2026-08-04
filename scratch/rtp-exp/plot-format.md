# FastReRoute 繪圖格式規範（format.md）

本文件從專案內 4 個 Python 繪圖腳本解析而來，作為後續修改圖片呈現的統一準則。

| 腳本 | 輸出圖檔 | 圖表類型 |
|------|----------|----------|
| `fig_5.py` | `fig_5.png` | 折線圖 |
| `time_throughput_10ms_20ms.py` | `time_throughput_10ms.png` / `time_throughput_20ms.png` / `time_throughput_10ms_20ms.png` | 折線圖 |
| `time_throughput_10ms_20ms_bar.py` | `time_throughput_10ms_bar.png` / `time_throughput_20ms_bar.png` | 長條圖 |
| `time_packet_loss_10ms_20ms_bar.py` | `time_packet_loss_10ms_bar.png` / `time_packet_loss_20ms_bar.png` | 長條圖 |

---

## 1. 全域樣式

| 項目 | 設定值 | 備註 |
|------|--------|------|
| Matplotlib 樣式 | `['science', 'ieee']` | 使用 `with plt.style.context(['science', 'ieee']):` |
| 後端（bar 腳本） | `matplotlib.use('Agg')` | 無 GUI 環境匯出用 |
| 圖片尺寸 | `figsize=(8, 5)` | 寬 8 吋 × 高 5 吋 |
| 輸出格式 | PNG | |
| 解析度 | `dpi=300` | |
| 儲存方式 | `plt.savefig(fig_name + '.png', dpi=300)` | |

### 字體

程式中**未手動指定** `font.family`，字體由 **SciencePlots + IEEE 樣式**自動決定（通常為適合論文的 serif 字體）。手動可調項目如下：

| 元素 | 字級 |
|------|------|
| X / Y 軸標籤 | **18**（`axes.xaxis.label.set_size(18)`） |
| X / Y 軸刻度 | **14**（`plt.xticks(fontsize=14)` / `plt.yticks(fontsize=14)`） |
| 圖例文字 | **14**（`fontsize=14`） |
| 圖例標題 | **14**（`title_fontsize=14`） |

---

## 2. 演算法／系列對應（label_sequence 優先順序）

**繪製順序 = `label_sequence` 列舉順序**：先畫的系列在底層，後畫的系列在上層（後蓋前）。

### 2.1 四系列圖（bar 圖 & 部分折線圖）

| 順序 (idx) | 標籤 | 顏色 | 折線 marker | 長條 hatch |
|:----------:|------|------|-------------|------------|
| 0 | `FastReRoute` | `b`（藍） | `-o`（圓形） | `"xxx"`（`"x"*3`） |
| 1 | `FastReRoute-C` | `y`（黃） | `-X`（叉形） | `"o"` |
| 2 | `FastReRoute-L` | `r`（紅） | `-p`（五邊形） | `"///"`（`"/"*3`） |
| 3 | `RSFR` | `m`（洋紅） | `-h`（六邊形） | `"---"`（`"-"*3`） |

### 2.2 八系列圖（`fig_5.py`：d10 + d20 各 4 條 f100/f200）

| 順序 (idx) | 標籤模式 | 顏色 | marker |
|:----------:|----------|------|--------|
| 0 | `FastReRoute-f100-d*` | `b` | `-o` |
| 1 | `FastReRoute-f200-d*` | `b` | `-o` |
| 2 | `FastReRoute-C-f100-d*` | `y` | `-X` |
| 3 | `FastReRoute-C-f200-d*` | `y` | `-X` |
| 4 | `FastReRoute-L-f100-d*` | `r` | `-p` |
| 5 | `FastReRoute-L-f200-d*` | `r` | `-p` |
| 6 | `RSFR-f100-d*` | `m` | `-h` |
| 7 | `RSFR-f200-d*` | `m` | `-h` |

`fig_5.py` 先畫 `label_sequence1`（d10），再畫 `label_sequence2`（d20），共 16 條線，marker／顏色索引 0–7 重複使用。

### 2.3 完整 color_set（供擴充系列）

```python
color_set = ['b', 'y', 'r', 'y', 'm', 'c',
             'k', 'dimgray', 'limegreen', 'yellow', 'g', 'red', 'plum']
```

八系列版（`fig_5.py`）前 8 項為：`['b','b','y','y','r','r','m','m']`。

### 2.4 完整 marker_dict

**四系列（折線）：**
```python
marker_dict = ["-o", "-X", "-p", "-h"]
```

**八系列（`fig_5.py`）：**
```python
marker_dict = ["-o", "-o", "-X", "-X", "-p", "-p", "-h", "-h"]
```

**備用（八系列含 10/20ms 對照，目前註解）：**
```python
# ["-o", "-o", "-X", "-X", "-p", "-p", "-h", "-h", "-p", "-P", "-+", "-*"]
```

### 2.5 完整 hatch_set（僅 bar 圖）

```python
hatch_set = ["x"*3, "o", "/"*3, "-"*3, ".", "|"*3, "\\"*3, "*", "+", "O"]
# 即：["xxx", "o", "///", "---", ".", "|||", "\\\\\\", "*", "+", "O"]
```

四系列 bar 圖使用前 4 項。

### 2.6 line_set（已定義但未實際使用）

```python
line_set = [(0, ()), 'dashdot', (0, (5, 1)), (0, (3, 1, 1, 1)),
            (0, (3, 1, 1, 1, 1, 1)), (0, (1, 5)), (0, (1, 1)), (0, (5, 5))]
```

目前折線圖的線型由下方「線型規則」決定，**未**引用 `line_set[idx]`。

---

## 3. 折線圖格式

### 3.1 線條

| 項目 | 值 |
|------|-----|
| 線寬 `linewidth` | **1.0** |
| 線型 `linestyle` | 見下方規則 |

**線型規則（依 X 軸資料點數 `len(xtick_set)`）：**

| 條件 | 線型 | 其他 |
|------|------|------|
| `len(xtick_set) >= 300` | 全部 `"-"`（實線） | `markevery=15` |
| `len(xtick_set) < 300` | **偶數 idx** → `"--"`（虛線） | |
| | **奇數 idx** → `"-"`（實線） | |

> `fig_5.py` 的 `xtick_set = [200,300,400,500,600,700]`（長度 6），走 `< 300` 分支。

### 3.2 Marker

| 項目 | 值 |
|------|-----|
| 大小 `markersize` | **6** |
| 樣式 | 取自 `marker_dict[idx]`（含線＋marker 字串，如 `"-o"`） |
| 空心／實心 | **奇數 idx**：`markerfacecolor='none'`（空心） |
| | **偶數 idx**：不設 `markerfacecolor`（實心填色） |
| 稀疏取點 | `len(xtick_set) >= 300` 時 `markevery=15` |

### 3.3 軸標籤（依腳本）

| 腳本 | X 軸 | Y 軸 |
|------|------|------|
| `fig_5.py` | Link bandwidth (Mbps) | Average packet loss rate (\%) |
| `time_throughput_10ms_20ms.py` | Time (s) | Average throughput (Mbps) |

### 3.4 X 軸刻度

| 條件 | 刻度設定 |
|------|----------|
| `30 <= len(xtick_set) < 300` | `range(0, 35, 5)` → 0, 5, 10, …, 30 |
| `len(xtick_set) >= 300` | `range(0, 305, 30)` → 0, 30, 60, …, 300 |
| 其他 | 直接使用 `xtick_set` |

Y/X 軸皆使用 `MaxNLocator(integer=True)`，刻度為整數。

### 3.5 圖例（折線圖）

| 腳本 | ncol | loc | bbox_to_anchor |
|------|:----:|-----|----------------|
| `fig_5.py` | 4 | `upper center` | `(0.512, -0.08)` |
| `time_throughput_10ms_20ms.py` | 4 | `center` | `(0.512, -0.08)` |

共通參數：
- `frameon=True`
- `fontsize=14`, `title_fontsize=14`
- `bbox_transform=fig.transFigure`
- `columnspacing=1`

> 兩腳本皆先呼叫 `plt.legend()` 再覆寫第二次；以最後一次為準。

---

## 4. 長條圖格式（bar）

### 4.1 長條樣式

| 項目 | 值 |
|------|-----|
| 填色 `color` | `'w'`（白色） |
| 邊框色 `edgecolor` | `color_set[idx]` |
| 紋理 `hatch` | `hatch_set[idx]` |
| 寬度 `width` | **0.5** |
| 對齊 `align` | `'center'` |
| 透明度 `alpha` | **0.9** |

### 4.2 分組位置

```python
xpos = np.arange(0, len(xtick_set) * 4, 4)   # 每組間距 4
interval = 0.6
xpos = xpos - 0.5 * interval * (num_of_label - 1)  # 四系列時 num_of_label=4
# 每個 label 畫完後：xpos = xpos + interval
```

| 項目 | 值 |
|------|-----|
| 每組 X 刻度 | `_xpos`（原始 `np.arange(0, n*4, 4)`） |
| 刻度標籤 | `xtick_set`（如 `[50, 100, 150, 200, 250]`） |

### 4.3 軸標籤

| 腳本 | X 軸 | Y 軸 |
|------|------|------|
| `time_throughput_10ms_20ms_bar.py` | Time (s) | Average throughput (Mbps) |
| `time_packet_loss_10ms_20ms_bar.py` | Time (s) | Average packet loss rate (\%) |

### 4.4 圖例（bar 圖）

| 項目 | 值 |
|------|-----|
| ncol | **10** |
| loc | `lower center` |
| bbox_to_anchor | `(0.512, -0.08)` |
| 其餘 | 同折線圖（frameon、fontsize=14 等） |

### 4.5 誤差棒（已註解，未啟用）

```python
# plt.errorbar(..., fmt='.', color='Black', elinewidth=0.5, capthick=5,
#              errorevery=1, alpha=0.9, ms=4, capsize=2)
```

---

## 5. 圖框（Axes / Spine）

- 未手動設定 `spines` 可見性、線寬或顏色。
- 圖框外觀由 **science + ieee** 樣式決定。
- 刻度強制整數：`MaxNLocator(integer=True)`（X、Y 皆套用）。

---

## 6. 線條／系列繪製順序總覽

```
for idx, label in enumerate(label_sequence):
    plt.plot(...) 或 plt.bar(...)
    # idx 越小越先畫（越在底層）
```

**fig_5.py 特殊順序：**
1. `label_sequence1`（d10，idx 0→7）
2. `label_sequence2`（d20，idx 0→7 重複）

---

## 7. 各腳本可調區塊（以 `#` 包圍）

修改圖表時，優先改腳本中以長串 `#` 標記的區塊：

| 區塊名稱 | 用途 |
|----------|------|
| `color_set` | 系列顏色 |
| `marker_dict` | 折線 marker |
| `hatch_set` | bar 紋理 |
| `line_set` | 線型（目前未使用） |
| `label_sequence` / `label_sequence1/2` | 系列順序與標籤 |
| `csv_file_name` | bar 腳本輸出 CSV 名稱 |
| `fig_name` | 輸出 PNG 名稱 |
| `decide data set` | 折線圖選擇 4/8 系列或 10ms/20ms |

---

## 8. 已知不一致（修改時可一併修正）

1. **fig_5.py d20 空心 marker**：d10 在 `idx%2==1` 設 `markerfacecolor='none'`，d20 的 else 分支**未**對奇數 idx 設空心，與 d10 不一致。
2. **plt.legend() 重複呼叫**：先預設再覆寫，可合併為一次。
3. **line_set 未使用**：若要用自訂虛線樣式，需改 plot 呼叫加入 `linestyle=line_set[idx]`。
4. **fig_5 八系列 color_set**：索引 1、3 重複用 `y`，5、7 重複用 `m` 等，與四系列 bar 圖配色邏輯不同（f100/f200 同色）。

---

## 9. 快速對照：修改圖片時的檢查清單

- [ ] 樣式：`science` + `ieee`
- [ ] 尺寸：8×5 吋，PNG 300 dpi
- [ ] 軸標籤 18pt，刻度 14pt，圖例 14pt
- [ ] 折線：linewidth=1.0，markersize=6
- [ ] 四系列顏色：藍 / 黃 / 紅 / 洋紅
- [ ] 四系列 marker：圓 / X / 五邊形 / 六邊形
- [ ] 奇數 idx 空心 marker；長序列 markevery=15
- [ ] 短序列（<300 點）：偶數 idx 虛線 `--`
- [ ] bar：白底 + 彩色邊框 + hatch，width=0.5，interval=0.6
- [ ] 圖例在圖下方：`bbox_to_anchor=(0.512, -0.08)`
- [ ] 系列順序依 `label_sequence`，後畫者在上層

---

*本文件對應程式版本：FastReRoute_YuKaiLee 專案內 4 個 `.py` 繪圖腳本。*

---

## 10. RTP 實驗繪圖守則（`scratch/rtp-exp/`）

本節為 RTP 實驗專案在 FastReRoute 格式基礎上的**額外規則**。共用實作位於 `plot_style.py`；腳本如下：

| 腳本 | 輸出圖檔 | 圖表類型 |
|------|----------|----------|
| `Exp1/plot-exp.py` | `compare-fps.png` / `compare-throughput.png` | 折線圖（四節點比較） |
| `Exp2/compare-exp.py` | `compare-fps.png` / `compare-throughput.png` | 時間序列 + 長條圖（各一張） |

### 10.1 X 軸：預設為時間

| 項目 | 規則 |
|------|------|
| **預設 X 軸** | CSV 欄位 `time_s`，標籤 **`Time (s)`** |
| **X 軸範圍** | 使用 `apply_time_axis()`；時間序列圖**上限為 100 s**（`TIME_PLOT_MAX = 100`），`time_s > 100` 的資料不繪製 |
| **資料裁切** | 繪圖前以 `clip_rows_to_time()` / `clip_xy_by_time()` 過濾 |
| **禁止誤用** | **不得**對 `time_s` 資料呼叫 `apply_xtick_steps()`（該函式為 FastReRoute 固定刻度，會強制 `range(0, 35, 5)`，導致只顯示前 30 秒） |
| **Sample index** | 僅在 `--sample-index` 選用時使用，標籤 **`Sample index`**；適用於跨節點時鐘不一致、需以樣本序號對齊的特殊比較 |

### 10.2 平均線：全寬虛線 + 不同 dash pattern

compare 圖與 Exp1 折線圖中的**平均值**以**貫穿全圖的水平虛線**呈現（`plot_average_marker()` → 內部 `ax.axhline`）。

| 元素 | 說明 |
|------|------|
| 線段 | **全寬**水平線（與先前虛線版相同） |
| 線型 | 各系列使用 `AVG_LINE_STYLES` 中**不同 dash pattern**（與系列 idx 對應），黑白列印仍可區分 |
| 線寬 / 透明度 | `linewidth=1.0`、`alpha=0.55` |
| 顏色 | 與對應折線系列同色（`COLOR_SET[idx]`） |

**禁止**所有系列共用同一種 `linestyle="--"` 而不區分 dash pattern。

**圖例（兩列）**：`add_legend_below(..., avg_items=...)` 在圖下方顯示兩列圖例：
1. 第一列：折線系列（marker + 實/虛線）
2. 第二列：各系列的 average 虛線樣式，標籤為 `{label} average`（例如 `sender average`、`No QoS average`）

### 10.3 縮寫規則（使用者可見文字）

- 同一圖或表格中，**反覆出現的詞**第一次寫全名，之後才縮寫。
- **只出現一次**的詞維持全名（例如 `Average throughput (kbps)`，不用 `Avg Thr`）。
- 常數定義見 `plot_style.py`：`YLABEL_AVG_FPS`、`YLABEL_AVG_THROUGHPUT`、`YLABEL_AVG_THROUGHPUT_SHORT` 等。

### 10.4 字型與依賴

| 項目 | 設定 |
|------|------|
| 樣式 | `science` + `ieee`（`plot_context()`） |
| 字型 | `_configure_fonts()` 註冊 Times New Roman，fallback 至 Nimbus Roman / Liberation Serif |
| TeX | `text.usetex = False`（避免缺少 LaTeX 時報錯） |
| 依賴 | 見 `requirements-plot.txt`：`matplotlib`、`SciencePlots` |

### 10.5 RTP 快速檢查清單

- [ ] X 軸預設 `Time (s)`，時間序列上限 100 s
- [ ] 未對 `time_s` 使用 `apply_xtick_steps()`
- [ ] 平均線為全寬虛線，各系列 dash pattern 不同；圖下方第二列圖例標示 `{label} average`
- [ ] 其餘沿用 §1–§9  "science + ieee"、8×5 吋、300 dpi、軸標籤 18pt、刻度 14pt
