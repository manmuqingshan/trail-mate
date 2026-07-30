#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        return 1;
    }

    const std::string path =
        std::string(argv[1]) +
        "/platform/esp/arduino_common/src/chat/infra/reticulum/reticulum_interfaces.cpp";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return 2;
    }
    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

    // A pending AsyncTcpConnector is a state machine that must be polled on
    // each runtime tick. It is not equivalent to an already-online socket.
    if (source.find("#if !TRAIL_MATE_RETICULUM_WIFI_CLIENT_AVAILABLE\n"
                    "    if (socket_open_pending_)") == std::string::npos)
    {
        return 3;
    }
    if (source.find("if (socket_open_pending_)\n    {\n        return;\n    }\n\n"
                    "    (void)ensureSocket();") != std::string::npos)
    {
        return 4;
    }
    if (source.find("const auto connect_status = connector_.poll(now_ms);") ==
        std::string::npos)
    {
        return 5;
    }
    return 0;
}
