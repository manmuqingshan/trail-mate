#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv)
{
    assert(argc == 2);

    std::ifstream source_file(argv[1], std::ios::binary);
    assert(source_file.is_open());
    const std::string source((std::istreambuf_iterator<char>(source_file)),
                             std::istreambuf_iterator<char>());

    const std::string text_value_route =
        "const bool text_value_uses_content_font =\n"
        "        widget.def->type == settings::ui::SettingType::Text &&\n"
        "        ::ui::fonts::utf8_has_non_ascii(value);";
    const std::size_t route_offset = source.find(text_value_route);
    assert(route_offset != std::string::npos);

    const std::size_t content_dispatch = source.find(
        "::ui::i18n::set_content_label_text_raw(widget.value_label, value);",
        route_offset);
    assert(content_dispatch != std::string::npos);
    return 0;
}
