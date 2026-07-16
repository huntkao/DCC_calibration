#include "dcc_seqview/viewer_state.hpp"

namespace dcc::seqview {

void ViewerState::open(const std::string& p) {
  result = load_file(p);
  loaded = true;
  if (result.seq.grid_h > 0 && sel_r >= result.seq.grid_h) sel_r = 0;
  if (result.seq.grid_w > 0 && sel_c >= result.seq.grid_w) sel_c = 0;
}

}  // namespace dcc::seqview
