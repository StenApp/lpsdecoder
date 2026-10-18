// lpsdecoder.cpp - LithTech PSX / PS2 LPS extractor (fixed folder handling + batch)

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include "LPS.h"
#include "cxxopts.hpp"

#ifdef _WIN32
#include <Windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

static bool EndsWithIgnoreCase(const std::string& s, const std::string& suffix)
{
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    }
    return true;
}

static std::string BaseName(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    std::string name = (pos == std::string::npos) ? path : path.substr(pos + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

static std::vector<std::string> ListLpsFiles(const std::string& dir)
{
    std::vector<std::string> result;

#ifdef _WIN32
    std::string pattern = dir;
    if (!pattern.empty() && pattern.back() != '\\' && pattern.back() != '/')
        pattern += "\\";
    pattern += "*";

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return result;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = fd.cFileName;
        if (EndsWithIgnoreCase(name, ".lps") || EndsWithIgnoreCase(name, ".lit")) {
            std::string full = dir;
            if (!full.empty() && full.back() != '\\' && full.back() != '/')
                full += "\\";
            full += name;
            result.push_back(full);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return result;
    while (dirent* ent = readdir(d)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        if (EndsWithIgnoreCase(name, ".lps") || EndsWithIgnoreCase(name, ".lit")) {
            std::string full = dir;
            if (!full.empty() && full.back() != '/')
                full += "/";
            full += name;
            result.push_back(full);
        }
    }
    closedir(d);
#endif

    std::sort(result.begin(), result.end());
    return result;
}

static int ProcessOne(const std::string& filename,
                      const std::string& pathToVGMStream,
                      bool extractOnly, bool cleanUp,
                      const std::string& outputRoot)
{
    std::cout << "\n>>> " << filename << "\n";

    LPS lps;
    if (!lps.Open(filename)) {
        std::cerr << "Failed to open " << filename << "\n";
        return 1;
    }

    std::cout << "Extracting to \"" << outputRoot << "/\" ...\n";
    lps.Extract(pathToVGMStream, extractOnly, cleanUp, outputRoot);
    lps.Close();
    return 0;
}

int main(int argc, char* argv[])
{
    bool showHelp = false;
    bool extractOnly = false;
    bool cleanUpSS2 = true;
    std::string filename;
    std::string batchDir;
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
    pathToVGMStream = "vgmstream-cli";
#endif

    cxxopts::Options options("lpsdecoder",
        "Decodes and extracts PS2 LithTech LPS sound archives\n"
        "(fixed folder handling – ENGLISH / AMERICAN / PLAYER / …)");

    options.add_options()
        ("h,help", "Show help", cxxopts::value(showHelp))
        ("f,file", "Single LPS/LIT file to extract", cxxopts::value(filename))
        ("b,batch", "Process all .lps/.lit files in a directory", cxxopts::value(batchDir))
        ("e,extract-only", "Only write .ss2 files, do not convert to WAV",
            cxxopts::value(extractOnly)->default_value("false"))
        ("c,clean-up-ss2", "Delete .ss2 after successful WAV conversion",
            cxxopts::value(cleanUpSS2)->default_value("true"))
        ("v,vgmstream", "Path to vgmstream / vgmstream-cli executable",
            cxxopts::value(pathToVGMStream))
        ;

    auto result = options.parse(argc, argv);

    if (showHelp) {
        std::cout << options.help() << "\n";
        std::cout << "Examples:\n"
                  << "  lpsdecoder -f t06s01.lps\n"
                  << "  lpsdecoder -b C:\\NOLF\\Sounds\n"
                  << "  lpsdecoder -b ./lps_files -e\n";
        return 0;
    }

    if (filename.empty() && batchDir.empty()) {
        std::cout << options.help() << "\n";
        std::cout << "Error: specify -f <file> or -b <directory>\n";
        return 1;
    }

    if (!filename.empty() && !batchDir.empty()) {
        std::cerr << "Error: use either -f or -b, not both\n";
        return 1;
    }

    // --- single file ---
    if (!filename.empty()) {
        return ProcessOne(filename, pathToVGMStream, extractOnly, cleanUpSS2, "out");
    }

    // --- batch mode ---
    std::vector<std::string> files = ListLpsFiles(batchDir);
    if (files.empty()) {
        std::cerr << "No .lps / .lit files found in \"" << batchDir << "\"\n";
        return 1;
    }

    std::cout << "Batch mode: " << files.size() << " file(s) in \"" << batchDir << "\"\n";

    int errors = 0;
    for (size_t n = 0; n < files.size(); ++n) {
        std::cout << "\n[" << (n + 1) << "/" << files.size() << "] ";
        // each LPS gets its own subfolder under out/
        std::string outRoot = "out";
#ifdef _WIN32
        outRoot += "\\";
#else
        outRoot += "/";
#endif
        outRoot += BaseName(files[n]);

        if (ProcessOne(files[n], pathToVGMStream, extractOnly, cleanUpSS2, outRoot) != 0) {
            ++errors;
        }
    }

    std::cout << "\n========== Batch Summary ==========\n";
    std::cout << "  Files processed : " << files.size() << "\n";
    std::cout << "  Errors          : " << errors << "\n";
    std::cout << "===================================\n";

    return errors > 0 ? 1 : 0;
}
