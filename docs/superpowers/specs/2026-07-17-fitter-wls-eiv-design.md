# 設計文件:反向/WLS/EIV fitter + q→σ 標定(實驗導向)

> 日期:2026-07-17 · 狀態:設計已核可,待實作計畫
> 對應:SPEC-005 §8 #2(EIV 偏差備忘)、SPEC-004 §3a.1(q→σ 標定前置)、
> SPEC-003 §5(fitter 可抽換)、開發紀錄 §7 保留待辦 #3

## 1. 動機與定位

回歸模型 `DAC = k·disp + b` 中,DAC 為下給 VCM 的指令位置(**精確無噪**),
disparity 為量測值(**含噪**)——噪聲全在自變數,屬典型 errors-in-variables。
前向 OLS 因此有斜率衰減偏差(SPEC-005 §8 #2 估 ≈0.4%,在 v1 預算內故採 OLS)。

因 DAC 無噪,**反向回歸**(擬合 `disp = a·DAC + c` 後取 k=1/a)之噪聲落在因變數,
無偏;且此時 quality 可經 q→σ 標定轉為每樣本 WLS 權重,進一步降變異。

**定位:實驗導向。** OLS 仍為預設(預設行為 bit-exact 不變);新增可抽換 fitter
與 q→σ 橋接,以 Sim 蒙地卡羅量化改善幅度,產出分析報告後再議是否換預設。

## 2. 架構決策

**採 options struct + enum**(與 `aggregate::Method` 同款),不引入虛介面:

- dcc_core 維持純函式風格,無 OO 注入先例(注入僅限 hal 層)
- config 字串直接映射 enum;SPEC-003 §5「fitter 可抽換」由 enum+config 滿足
- 既有呼叫點零改動(舊簽名保留,語意 ≡ 前向 OLS)

否決:每 fitter 一函式(API 面積 ×4、分派散落)、std::function 注入(過度設計)。

## 3. core:regression API 擴充

```cpp
namespace dcc::regression {

enum class Fitter { ols_forward, ols_inverse, wls_inverse, deming };

struct FitOptions {
  Fitter method = Fitter::ols_forward;
  const std::vector<double>* weights = nullptr;  // 每幀權重(nullptr=等權;僅 wls_inverse 用)
  double deming_delta = 0.0;                     // δ = σ_DAC²/σ_disp²(0 = DAC 無噪之極限)
};

// 既有簽名保留(≡ ols_forward)
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     int min_valid_samples = 8);
// 新增 overload
FitResult fit_region(const std::vector<int>& dacs, const std::vector<double>& disp,
                     const FitOptions& opts, int min_valid_samples = 8);
}
```

數學(記 x=disp、y=DAC;中心化矩 sxx_c、sxy_c、syy_c;截距統一 `b = ȳ − k·x̄`):

| fitter | 斜率 k | 性質 |
|---|---|---|
| ols_forward | sxy_c / sxx_c | 現況;x 含噪 → 衰減偏差 |
| ols_inverse | syy_c / sxy_c | 噪聲在因變數 → 無偏 |
| wls_inverse | 加權矩之 syy_w / sxy_w | 無偏 + 最小變異(權重正確時) |
| deming | 標準閉式解(含 δ) | δ→0 代數上 ≡ 反向;δ→∞ ≡ 前向 |

- r² 用 Pearson(對稱,四者共用);n_valid 沿用非 NaN 計數。
- 錯誤處置沿用:樣本不足 E_D03、k≤0 E_E01(四 fitter 一致)。
- WLS 權重語意:w≤0 或 NaN 之樣本形同剔除(與 SPEC-004 §3a.1 q=0 語意一致);
  另要求 Σw > 0 否則 E_D03。

## 4. core:qsigma 殘差式標定(新模組)

```cpp
// 輸入:展平之 (q, |殘差|) 樣本對(殘差由呼叫端以反向 OLS 初擬合取得)
// 程序:按 q 等頻分箱(預設 8)→ 每箱 σ̂ → log σ̂ vs log q 直線擬合
// 輸出:σ(q) = σ₀·q^(−p)
namespace dcc::qsigma {
struct Result { double sigma0, p, r2; int bins_used; };
Result calibrate(const std::vector<double>& q, const std::vector<double>& abs_resid,
                 int n_bins = 8);
}
```

**橋接關鍵**:w = 1/σ(q)² ∝ q^(2p) → **標定產物即參數化權重之 γ = 2p**。
Sim 誠實模型(σ_eff = σ₀/√q)⇒ p = 0.5 ⇒ γ = 1,兩半題目閉環自洽。
真實 SAD quality(曲線形狀導出)到位後,跑同一標定程序即得該資料之 γ。

## 5. config / 管線 / GUI 接點

- config 新增(dcc_io/config,含驗證):
  - `regression.fitter`:string,`"ols_forward"`(預設)|`"ols_inverse"`|`"wls_inverse"`|`"deming"`
  - `regression.weight_gamma`:double,預設 1.0(僅 wls_inverse 用;w = q^γ)
- pipeline:依 cfg 組 FitOptions;wls_inverse 時逐區權重 = quality[f][ri]^γ;
  序列無 quality → 退化等權(≡ ols_inverse)並記警告。
- report.json 參數快照記 fitter/γ(可追溯)。
- GUI 參數面板加 fitter 下拉 + γ 輸入(CLI 走 config,能力等價)。
- Deming 之 δ 不進 config(管線用 δ=0 ≡ 反向;δ 掃描僅實驗面 CLI 用)。

## 6. CLI `--fitter-scan`

仿既有 `--scan`:蒙地卡羅掃 噪聲 σ × 四 fitter × γ∈{0.5, 1, 2}(僅 WLS 軸),
synth 用 `quality_model=focus_linked`(q 逐幀變化才顯 WLS 增益),固定 seed 集。
每組合輸出:中央區 DCC 偏差 bias%、變異 CV%、全區 RMSE%;
每 σ 另跑 qsigma 標定,輸出回推之 p̂/σ̂₀。落 CSV(`--out` 指定路徑)。

## 7. 測試(Catch2,tag [fitter][qsigma],固定 seed 全確定性)

| ID | 內容 | 準則 |
|---|---|---|
| UT-F1 | 無噪四 fitter | 皆 k=12.46/b=420 至 1e-6;deming(δ=0) ≡ 反向至 1e-12 |
| UT-F2 | 大噪聲消偏 | 前向偏差符合理論衰減式;\|反向偏差\| ≪ \|前向偏差\| |
| UT-F3 | WLS 效率 | 誠實 quality 下 var(WLS) < var(等權反向)(保守斷言防 flaky) |
| UT-Q1 | qsigma 回推 | p ∈ [0.4, 0.6]、σ₀ 誤差 < 10% |
| UT-Q2 | 標定閉環 | calibrate 之 γ=2p 餵 WLS ≈ 真 γ=1 之 WLS |
| IT | 管線接點 | `fitter=ols_inverse` dry-run PASS;預設 config 下既有 80 案例不動 |

## 8. 交付與文件同步

1. 實驗跑完寫 `docs/fitter實驗_反向WLS與EIV.md`:動機、四 fitter 數學、
   q→σ 標定方法、掃描結果表、**結論(是否建議換預設 fitter)**。
2. 規格同步(勘誤流程):SPEC-003 §5 note(fitter enum 落地)、
   SPEC-005 §8 #2(實驗完成註記)、開發紀錄 §7 保留待辦 #3 收斂。

## 9. 範圍外(YAGNI)

- 不改預設 fitter(實驗結論出來另議)
- 不做逐樣本異方差 Deming(δ 僅標量;WLS 已覆蓋異方差需求)
- 不動既有 aggregate 之 weighted_mean 語意
- 不做 GUI 端 fitter 比較視覺化(CSV + 報告足夠;需要時另案)
