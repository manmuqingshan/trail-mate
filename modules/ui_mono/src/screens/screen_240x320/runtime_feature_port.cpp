#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

namespace ui::mono::screens::screen_240x320
{
namespace
{

RuntimeFeaturePort* s_runtime_feature_port = nullptr;

} // namespace

void installRuntimeFeaturePort(RuntimeFeaturePort* port)
{
    s_runtime_feature_port = port;
}

RuntimeFeaturePort* runtimeFeaturePort()
{
    return s_runtime_feature_port;
}

} // namespace ui::mono::screens::screen_240x320
