#include "platform/ui/device_runtime.h"
#include "platform/ui/time_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "platform/ui/lora_runtime.h"
#include <ctime>
namespace platform::ui::device {
MemoryStats memory_stats(){return {512*1024,260*1024,8*1024*1024,6*1024*1024,true};}
bool supports_screen_brightness(){return true;}
bool supports_keyboard_backlight(){return true;}
static uint8_t brightness=200,keys=0;
uint8_t screen_brightness(){return brightness;}
uint8_t screen_brightness_max(){return 255;}
void set_screen_brightness(uint8_t value){brightness=value;}
uint8_t keyboard_backlight(){return keys;}
uint8_t keyboard_backlight_max(){return 255;}
void set_keyboard_backlight(uint8_t value){keys=value;}
}
namespace platform::ui::lora {bool is_supported(){return true;}}
namespace platform::ui::gps {bool is_enabled(){return true;} bool is_powered(){return true;}}
