// lpsdecoder.cpp - LithTech PSX / PS2 LPS extractor (fixed folder handling)

#include <iostream>
#include <string>
#include "LPS.h"
#include "cxxopts.hpp"

#ifdef _WIN32
#include <Windows.h>
#endif

int main(int argc, char* argv[])
{
    bool showHelp = false;
    bool extractOnly = false;
    bool cleanUpSS2 = true;
    std::string filename;
    std::string pathToVGMStream;

#ifdef _WIN32
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
#ifdef _DEBUG
    pathToVGMStream = std::string(buffer) + "\\..\\Tools\\VGMStream\\test.exe";
#else
    pathToVGMStream = std::string(buffer) + "\\VGMStream\\test.exe";
#endif
#else
    // On Linux look for vgmstream-cli in PATH or common locations
    pathToVGMStream = "vgmstream-cli";
#endif

    cxxopts::Options options("lpsdecoder",
        "Decodes and extracts PS2 LithTech LPS sound archives\n"
        "(fixed folder handling – ENGLISH / AMERICAN / PLAYER / …)");

    options.add_options()
        ("h,help", "Show help", cxxopts::value(showHelp))
        ("f,file", "LPS file to extract", cxxopts::value(filename))
        ("e,extract-only", "Only write .ss2 files, do not convert to WAV",
            cxxopts::value(extractOnly)->default_value("false"))
        ("c,clean-up-ss2", "Delete .ss2 after successful WAV conversion",
            cxxopts::value(cleanUpSS2)->default_value("true"))
        ("v,vgmstream", "Path to vgmstream / vgmstream-cli executable",
            cxxopts::value(pathToVGMStream))
        ;

    auto result = options.parse(argc, argv);

    if (showHelp || filename.empty()) {
        std::cout << options.help() << "\n";
        if (filename.empty() && !showHelp) {
            std::cout << "Error: you must specify an LPS file with -f / --file\n";
            return 1;
        }
        return 0;
    }

    LPS lps;
    if (!lps.Open(filename)) {
        std::cerr << "Failed to open " << filename << "\n";
        return 1;
    }

    std::cout << "Extracting...\n";
    lps.Extract(pathToVGMStream, extractOnly, cleanUpSS2);
    lps.Close();

    return 0;
}
