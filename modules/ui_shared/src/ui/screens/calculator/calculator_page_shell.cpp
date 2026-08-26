#include "ui/screens/calculator/calculator_page_shell.h"

#include "ui/screens/calculator/calculator_page_runtime.h"

namespace calculator::ui::shell
{

void enter(void* user_data, lv_obj_t* parent)
{
    runtime::enter(static_cast<const Host*>(user_data), parent);
}

void exit(void* user_data, lv_obj_t* parent)
{
    (void)user_data;
    runtime::exit(parent);
}

} // namespace calculator::ui::shell
