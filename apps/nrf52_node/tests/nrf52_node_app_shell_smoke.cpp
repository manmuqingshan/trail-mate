#include "nrf52_node_app_shell.h"

#include <cassert>
#include <cstring>

int main()
{
    trailmate::apps::nrf52_node::Nrf52NodeAppShell shell;
    assert(shell.validate());

    const auto& config = shell.config();
    assert(std::strcmp(config.target_id, "gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(shell.targetId(), "gat562_mesh_evb_pro") == 0);
    assert(std::strcmp(config.target_family, "nrf52_node") == 0);
    assert(std::strcmp(config.default_ux_pack_id, "tiny_node_status") == 0);
    assert(std::strcmp(shell.activeUxPackId(), "tiny_node_status") == 0);
    assert(shell.targetProfile() != nullptr);
    assert(shell.targetProfile()->renderer == product_composition::TargetRenderer::Headless);
    return 0;
}
