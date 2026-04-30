#include "xml_to_c.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static std::string slurp(std::istream& in) {
    return {std::istreambuf_iterator<char>(in), {}};
}

int main(int argc, char* argv[]) {
    std::string prefix     = "generated";
    std::string input_path;

    if (argc >= 2) prefix     = argv[1];
    if (argc >= 3) input_path = argv[2];

    std::string xml;
    if (input_path.empty()) {
        xml = slurp(std::cin);
    } else {
        std::ifstream f(input_path);
        if (!f) {
            std::cerr << "xml_to_c: cannot open '" << input_path << "'\n";
            return EXIT_FAILURE;
        }
        xml = slurp(f);
    }

    try {
        std::cout << XmlToC::convert(xml, prefix);
        return EXIT_SUCCESS;
    } catch (const std::exception& ex) {
        std::cerr << "xml_to_c error: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
}