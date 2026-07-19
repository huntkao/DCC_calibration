# 設計文件:chart 距離公差 → DCC 靈敏度換算工具

> 日期:2026-07-18 · 狀態:設計已核可,待實作計畫
> 對應:SPEC-005 §7 開放問題 #3、CLAUDE.md 開放問題 #3、開發紀錄 §7 保留待辦 #1
> 定位:離線分析/實驗工具(2026-07-17 範圍決議);不碰硬體,真實 nl / VCM 標定由外部提供。

## 1. 問題:兩個獨立誤差源,現有框架只做了一個

「產線治具 chart 距離公差對 DCC 的靈敏度」牽涉兩個獨立物理量:

| 環節 | 物理成因 | 現有框架 | 狀態 |
|---|---|---|---|
| ① disparity 曲線非線性 → 把「合焦偏移(DAC)」轉譯成「DCC 誤差」 | 鏡頭球差(nl2)、PD 角度響應飽和/模糊圈超窗(nl3) | `--scan` + synth 之 nl2/nl3 模型(2026-07-12 M1c) | ✅ 已驗證公式吻合 |
| ② chart 物理距離誤差(cm)→ 轉成「合焦偏移(DAC)」 | 治具擺放幾何誤差 | 無——PHASE B-1 僅文字「查本鏡頭 DAC↔物距對照」,程式零實作 | ❌ 缺口 |

**因果關係**(開發紀錄 §3.3 驗證):理想線性(nl=0)時 DCC 對合焦偏移完全不敏感——
斜率不變、只動截距。chart 距離公差本身不是 nl 的成因;二者是**平行的獨立誤差源,
經由 nl 這個傳導係數耦合**。只有 nl≠0 時,chart 距離公差才轉譯為 DCC 誤差。

**本案交付**:補上 ②,把 ①+② 串起來雙向換算:
`cm 公差 →(②)→ DAC offset →(①)→ ΔDCC%`,並反向從「可接受 ΔDCC%」回推「可接受 ±cm 公差」。

## 2. 核心:VCM DAC↔物距轉換模型

VCM 把 DAC 換成鏡頭位移,配合成像高斯公式決定合焦物距。薄透鏡近似下
**DAC 與 1/物距 呈線性**(VCM 校正常識:DAC_INF≈1/∞、DAC_MACRO≈1/最近物距):

```
DAC = a + b / dist_cm
```

以**兩點標定**求 a、b(給鏡頭專屬的 INF/MACRO 兩個 DAC↔物距對照點):

```
b = (dac1 − dac2) / (1/dist1 − 1/dist2)
a = dac1 − b / dist1
```

**誠實性處理(鐵律 5)**:兩個對照點是鏡頭專屬光學特性,本專案拿不到真實值。
模型參數一律走 **config / CLI 輸入 + 明確標為「示範/佔位值」**,程式不內建任何
假裝為真的數字。示範標定用「典型模組」合理範圍(如 INF 取遠點 200cm、MACRO 10cm),
每個輸出都標注「示範值,實機須換」。

新增純函式模組 `src/dcc_core/chart_dist`(無 I/O/UI,依賴 error.hpp):

```cpp
// 輸入單位:dac = DAC(int/double)、dist = 物距 cm(正值)。
// 輸出單位:同上。DAC = a + b/dist_cm(薄透鏡近似;兩點標定)。
namespace dcc::chart_dist {
struct VcmDistModel { double a, b; };  // DAC = a + b/dist_cm
VcmDistModel calibrate_two_point(double dac1, double dist1_cm, double dac2, double dist2_cm);
double dac_to_dist(const VcmDistModel&, double dac);      // DAC → 物距 cm
double dist_to_dac(const VcmDistModel&, double dist_cm);  // 物距 cm → DAC
}
```

錯誤處置:兩點 DAC 相同、物距 ≤ 0、或 b=0(退化)→ `DccError(E_A01)`。

## 3. 換算流程與 CLI 交付

新增 CLI 模式 `--chart-tol`(沿用 `--dry-run` 合成路徑;串接現有 `--scan` 的中央 4 區 DCC 演算法,不重寫):

**正算(cm 公差 → ΔDCC%)**:掃 chart 距離偏移,輸出對照表
```
# 示範標定值,實機須替換 --vcm-cal
距離_cm, delta_cm, offset_dac, central_dcc, delta_dcc_pct, status
```

**反算(可接受 ΔDCC% → ±cm 公差)**:給 DCC 誤差預算,二分搜出對應 ±cm 公差,
直接輸出「chart 擺放需準到 ±X cm」。二分在「單邊 cm 偏移」上做(靈敏度單調)。

CLI 介面:
```
dcc_cal --dry-run --chart-tol \
  --nl 0.05 \                    # 實模組非線性(必填,無預設)
  --vcm-cal 220:200,620:10 \     # 兩點標定 DAC:dist_cm(示範值;預設帶示範標定但印警告)
  --chart-dist 25 \              # chart 標稱擺放距離 cm(預設由 focus_center 經模型反推)
  --dcc-budget 1.0 \             # 可接受中央 DCC 誤差 %(反算用;預設 1.0)
  --scan-range-cm 3 --scan-steps 25
```

**設計要點**:
- `--nl` **不給預設值**:未帶 → exit 3 + 提示「這是實模組量測輸入,無預設值」。避免使用者
  誤以為有預設 nl 就是真值(鐵律 5)。
- `--vcm-cal` 有示範預設(220:200,620:10),但每次輸出印 `# 示範標定值,實機須替換` 一行。
- chart 標稱距離對應的 DAC = focus_center(掃描窗中點,合焦設計點);偏移 = dist_to_dac(標稱±Δcm) − dist_to_dac(標稱)。
- 沿用既有中央 4 區 {19,20,27,28} DCC 演算法與 synth nl 模型。

## 4. 測試(Catch2,tag [chart_dist],固定 seed 全確定性)

| ID | 內容 | 準則 |
|---|---|---|
| UT-C1 | 兩點標定往返 | dac→dist→dac、dist→dac→dist 至 1e-9;DAC 與 1/dist 線性性質 |
| UT-C2 | 退化輸入 | 兩點 DAC 相同 / 物距 ≤0 → E_A01 |
| UT-C3 | 反算閉環 | 給 nl+預算,回推 cm 公差代回正算命中預算(±5% 相對容差) |
| IT-C1 | --chart-tol 正算 | offset 越大 ΔDCC 越大(單調);nl=0 → ΔDCC ≡ 0(理想線性不敏感);缺 --nl → exit 3 |

## 5. 交付與文件同步

1. **分析文件** `docs/chart距離公差分析.md`:兩獨立誤差源、DAC↔物距模型數學、
   串接流程、示範對照表、**如何替換為真實值的步驟**、與開放問題 #3 的關係。
2. **投影片** `docs/chart公差_投影片.html`:同 fitter 那套 light 版面 / 系統字型 / 方向鍵 /
   零外部資源,約 8–10 頁(封面結論 → 兩誤差源拆解 → DAC↔物距模型 → 串接鏈路 →
   正算表 → 反算 demo → step-by-step 上線演示 → 交付/限制誠實標示)。
3. **規格同步**(勘誤流程):SPEC-005 §7 #3 更新為「換算工具已落地(正/反算 + 兩點標定);
   真實 nl 與 VCM 標定待外部,依離線+實驗定位屬外部承擔」;CLAUDE.md 開放問題 #3、
   開發紀錄 §7 保留待辦 #1 標記完成。

## 6. 範圍外(YAGNI)

- 不做 GUI 端 chart-tol 面板(既有靈敏度掃描面板已涵蓋 ① 的視覺化;cm 換算走 CLI + 報告)。
- 不內建任何真實鏡頭 DAC↔物距曲線庫(只提供兩點標定介面;非薄透鏡的高階模型待需要再議)。
- 不改 synth nl 模型、不改既有 --scan(串接沿用)。
- 不做多鏡頭批次換算(單組標定 + 單一 nl;批次需要時另案)。
