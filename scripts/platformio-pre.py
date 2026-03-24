Import("env")
import os
import subprocess
import sys
import json

print("[pio] pre: configuring build environment")

project_dir = env.get("PROJECT_DIR")
pio_platform = (env.get("PIOPLATFORM") or "").lower()
is_esp32_env = "espressif32" in pio_platform
is_nrf52_env = "nordicnrf52" in pio_platform


def configure_radiolib_for_gat562():
    if env.get("PIOENV") != "gat562_mesh_evb_pro":
        return

    project_dir = env.get("PROJECT_DIR")
    radiolib_dir = os.path.join(project_dir, ".pio", "libdeps", env.get("PIOENV"), "RadioLib")
    library_json_path = os.path.join(radiolib_dir, "library.json")

    if not os.path.exists(library_json_path):
        print(f"[pio] pre: RadioLib metadata not found yet: {library_json_path}")
        return

    with open(library_json_path, "r", encoding="utf-8") as fp:
        library_json = json.load(fp)

    build_section = dict(library_json.get("build", {}))
    desired_src_filter = [
        "-<*>",
        "+<Hal.cpp>",
        "+<Module.cpp>",
        "+<hal/Arduino/ArduinoHal.cpp>",
        "+<protocols/PhysicalLayer/*.cpp>",
        "+<protocols/Print/*.cpp>",
        "+<utils/*.cpp>",
        "+<modules/SX126x/SX1262.cpp>",
        "+<modules/SX126x/SX126x.cpp>",
        "+<modules/SX126x/SX126x_LR_FHSS.cpp>",
        "+<modules/SX126x/SX126x_commands.cpp>",
        "+<modules/SX126x/SX126x_config.cpp>",
    ]

    if build_section.get("srcFilter") == desired_src_filter:
        print("[pio] pre: RadioLib srcFilter already trimmed for gat562_mesh_evb_pro")
        return

    build_section["srcFilter"] = desired_src_filter
    library_json["build"] = build_section

    with open(library_json_path, "w", encoding="utf-8", newline="\n") as fp:
        json.dump(library_json, fp, indent=4)
        fp.write("\n")

    print("[pio] pre: Trimmed RadioLib build to SX126x-only sources for gat562_mesh_evb_pro")


configure_radiolib_for_gat562()

# Only ESP Arduino builds need the shared LVGL config under platform/esp.
if is_esp32_env:
    ui_dir = os.path.join(project_dir, "platform", "esp", "arduino_common", "include")
    env.Append(CPPFLAGS=["-I" + ui_dir])
    env.Append(CCFLAGS=["-I" + ui_dir])
    env.Append(CPPDEFINES=["LV_CONF_INCLUDE_SIMPLE"])
    print(f"[pio] pre: Added LVGL config include path: {ui_dir}")

# Nordic builds should use their own shared runtime headers instead of falling
# back to ESP include roots via unrelated global injection.
if is_nrf52_env:
    nrf_ui_dir = os.path.join(project_dir, "platform", "nrf52", "arduino_common", "include")
    env.Append(CPPFLAGS=["-I" + nrf_ui_dir])
    env.Append(CCFLAGS=["-I" + nrf_ui_dir])
    print(f"[pio] pre: Added nRF52 runtime include path: {nrf_ui_dir}")

# On nordicnrf52/Windows builds, `${platformio.packages_dir}` inside `build_flags`
# can be rewritten into an invalid builder-local include path. Inject the
# nRF52 framework paths here using the resolved absolute package location so
# framework libraries like `Wire` can include `<Adafruit_TinyUSB.h>`.
if is_nrf52_env:
    platform = env.PioPlatform()
    framework_dir = platform.get_package_dir("framework-arduinoadafruitnrf52")
    if framework_dir:
        framework_include_candidates = [
            os.path.join(framework_dir, "libraries", "Adafruit_TinyUSB_Arduino", "src"),
            os.path.join(framework_dir, "libraries", "Adafruit_TinyUSB_Arduino", "src", "arduino"),
            os.path.join(framework_dir, "libraries", "SPI"),
            os.path.join(framework_dir, "libraries", "Wire"),
            os.path.join(framework_dir, "libraries", "Bluefruit52Lib", "src"),
            os.path.join(framework_dir, "libraries", "Bluefruit52Lib", "src", "services"),
            os.path.join(framework_dir, "libraries", "Adafruit_nRFCrypto", "src"),
            os.path.join(framework_dir, "libraries", "Adafruit_LittleFS", "src"),
            os.path.join(framework_dir, "libraries", "InternalFileSytem", "src"),
        ]
        existing_cpppath = set(env.get("CPPPATH", []))
        include_paths = [path for path in framework_include_candidates if os.path.isdir(path)]
        missing_paths = [path for path in include_paths if path not in existing_cpppath]
        if missing_paths:
            env.Append(CPPPATH=missing_paths)
            print(f"[pio] pre: Added nRF52 framework include paths: {', '.join(missing_paths)}")

# Generate protobuf files if .proto files exist
proto_dir = os.path.join(project_dir, "lib", "meshtastic_protobufs")
generate_script = os.path.join(project_dir, "scripts", "generate_protobuf.py")

if os.path.exists(generate_script) and os.path.exists(proto_dir):
    print("[pio] pre: Generating protobuf files...")
    try:
        result = subprocess.run([sys.executable, generate_script], 
                              cwd=project_dir, capture_output=True, text=True)
        if result.returncode == 0:
            print("[pio] pre: Protobuf generation successful")
        else:
            print(f"[pio] pre: Protobuf generation warning: {result.stderr}")
    except Exception as e:
        print(f"[pio] pre: Protobuf generation error: {e}")

# Add generated protobuf include path
generated_dir = os.path.join(project_dir, "src", "chat", "infra", "meshtastic", "generated")
if os.path.exists(generated_dir):
    env.Append(CPPFLAGS=["-I" + generated_dir])
    env.Append(CCFLAGS=["-I" + generated_dir])
    print(f"[pio] pre: Added protobuf generated include path: {generated_dir}")

# Example hook: add a build flag to verify hook execution
env.Append(CPPDEFINES=["PIO_PRE_HOOK=1"])
