#include "board/BoardBase.h"
#include "platform/ui/device_runtime.h"
#include <emscripten.h>
EM_JS(void, preview_power_off, (), {if(Module.onPowerOff)Module.onPowerOff();});
class BrowserBoard final : public BoardBase {
public:
 uint32_t begin(uint32_t) override{return 0;}
 void wakeUp() override{}
 void handlePowerButton() override{}
 void softwareShutdown() override{preview_power_off();}
 void setBrightness(uint8_t value) override{platform::ui::device::set_screen_brightness(value);}
 uint8_t getBrightness() override{return platform::ui::device::screen_brightness();}
 bool hasKeyboard() override{return true;}
 void keyboardSetBrightness(uint8_t value) override{platform::ui::device::set_keyboard_backlight(value);}
 uint8_t keyboardGetBrightness() override{return platform::ui::device::keyboard_backlight();}
 bool isRTCReady() const override{return true;}
 bool isCharging() override{return false;}
 int getBatteryLevel() override{return 85;}
 bool isSDReady() const override{return true;}
 bool isCardReady() override{return true;}
 bool isGPSReady() const override{return true;}
 bool hasGPSHardware() const override{return true;}
 bool hasSstvAudioInput() const override{return true;}
 void vibrator() override{}
 void stopVibrator() override{}
};
static BrowserBoard browser_board;
BoardBase& board=browser_board;
