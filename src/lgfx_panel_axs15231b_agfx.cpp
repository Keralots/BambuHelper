// Out-of-line definition for Panel_AXS15231B_AGFX::_te_sem. Kept in a
// separate .cpp (rather than `inline` in the header) because the project
// is compiled in C++14 mode, which does not support inline static data
// members. See lgfx_panel_axs15231b_agfx.hpp for rationale.

#include "lgfx_panel_axs15231b_agfx.hpp"

SemaphoreHandle_t lgfx::v1::Panel_AXS15231B_AGFX::_te_sem = nullptr;
