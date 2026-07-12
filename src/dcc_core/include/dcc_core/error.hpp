// 具名錯誤碼與例外(SPEC-001 §3,滾動式清單)。
// core 以具名例外上拋,app/前端層轉為操作員訊息 + log(SPEC-003 §6)。
#pragma once

#include <stdexcept>
#include <string>

namespace dcc {

enum class ErrorCode {
  E_A01,  // AF 校正值缺失/非法
  E_C01,  // sweep 超出 DAC 範圍
  E_D01,  // 序列維度/形狀閉環驗算失敗
  E_D02,  // 序列 DAC/unit/pitch 與規劃或 config 不一致
  E_D03,  // 區域有效樣本不足
  E_E01,  // 回歸斜率非正(LEFT/RIGHT 顛倒?)
  E_F01,  // focus 峰值出界
  E_F02,  // 誤差超容差
  E_G01,  // Q-format 溢位
};

const char* to_string(ErrorCode code);

class DccError : public std::runtime_error {
 public:
  DccError(ErrorCode code, const std::string& msg)
      : std::runtime_error(std::string(to_string(code)) + ": " + msg), code_(code) {}

  ErrorCode code() const { return code_; }

 private:
  ErrorCode code_;
};

}  // namespace dcc
