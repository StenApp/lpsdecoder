#include "LPS.h"
#include <iostream>
#include <filesystem>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <Windows.h>
#else
// simple replacements for non-Windows
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace fs = std::filesystem;

LPS::LPS() = default;

LPS::~LPS()
{
    Close();
}

bool LPS::Open(const std::string& filename)
{
    m_Stream.open(filename, std::ios::binary);
    if (!m_Stream.is_open()) {
        return false;
    }

    // Read header (52 bytes)
    m_Stream.read(reinterpret_cast<char*>(&m_Header), sizeof(m_Header));
    if (m_Stream.gcount() != sizeof(m_Header)) {
        std::cerr << "Failed to read header\n";
        return false;
    }

    // Sanity check
    if (std::strncmp(m_Header.id, "LithTech PSX Sound Data 1.20", 28) != 0) {
        std::cerr << "Not a valid LithTech PSX Sound Data file\n";
        return false;
    }

    std::cout << "Folders: " << m_Header.folders << "  Files: " << m_Header.files << "\n";

    // Read file entries
    m_Files.resize(m_Header.files);
    for (uint32_t i = 0; i < m_Header.files; ++i) {
        m_Stream.read(reinterpret_cast<char*>(&m_Files[i]), sizeof(FileEntry));
    }

    // Read folder entries
    m_Folders.resize(m_Header.folders);
    for (uint32_t i = 0; i < m_Header.folders; ++i) {
        m_Stream.read(reinterpret_cast<char*>(&m_Folders[i]), sizeof(FolderEntry));
    }

    // Audio data starts right after the directory tables
    m_BaseOffset = static_cast<uint32_t>(m_Stream.tellg());
    std::cout << "Audio base offset: 0x" << std::hex << m_BaseOffset << std::dec << "\n";

    return true;
}

bool LPS::Close()
{
    if (m_Closed) return true;
    if (m_Stream.is_open()) m_Stream.close();
    m_Closed = true;
    return true;
}

std::string LPS::CleanFolderName(const char* raw, size_t maxLen)
{
    std::string name;
    for (size_t i = 0; i < maxLen; ++i) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        if (c == 0) break;
        // only keep printable ASCII, skip path separators
        if (c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':' && c != '*') {
            name += static_cast<char>(c);
        } else {
            break; // stop at first non-printable (common in these files)
        }
    }
    // trim trailing spaces/dots
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    if (name.empty()) name = "unknown";
    return name;
}

void LPS::Extract(const std::string& pathToVGMStream, bool extractOnly, bool cleanUp)
{
    // Build a map: fileIndex -> folder name
    // Only leaf folders that actually own files are used.
    std::map<uint32_t, std::string> fileToFolder;
    std::set<uint32_t> assigned;

    for (const auto& folder : m_Folders) {
        if (folder.files == 0) continue;

        uint32_t start = folder.fileStartIndex;
        uint32_t count = folder.files;

        if (start >= m_Header.files || start + count > m_Header.files) {
            std::cerr << "Warning: folder has invalid file range ("
                      << start << " + " << count << ")\n";
            continue;
        }

        std::string folderName = CleanFolderName(folder.foldername, 32);

        for (uint32_t i = start; i < start + count; ++i) {
            // later folders can override earlier ones if ranges overlap
            // (shouldn't happen with correct data, but be safe)
            fileToFolder[i] = folderName;
            assigned.insert(i);
        }
    }

    // Extract every file
    for (uint32_t i = 0; i < m_Header.files; ++i) {
        const FileEntry& file = m_Files[i];

        if (file.size == 0) {
            std::cout << "  [" << i << "] SKIP zero-size\n";
            continue;
        }

        // Determine output folder
        std::string folderName = "unassigned";
        auto it = fileToFolder.find(i);
        if (it != fileToFolder.end()) {
            folderName = it->second;
        }

        // Clean filename
        std::string origName(file.filename, strnlen(file.filename, 0x20));
        // strip extension for rebuilding
        std::string stem = origName;
        auto dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        if (stem.empty()) stem = "unnamed_" + std::to_string(i);

        // Build paths
        fs::path outDir = fs::path("out") / folderName;
        fs::create_directories(outDir);

        fs::path ss2Path = outDir / (stem + ".ss2");
        fs::path wavPath = outDir / (stem + ".wav");

        // Avoid overwriting if same name appears twice in same folder
        int suffix = 1;
        while (fs::exists(ss2Path) || fs::exists(wavPath)) {
            ss2Path = outDir / (stem + "_" + std::to_string(suffix) + ".ss2");
            wavPath = outDir / (stem + "_" + std::to_string(suffix) + ".wav");
            ++suffix;
        }

        // Read audio data
        uint32_t seekPos = file.offset + m_BaseOffset;
        m_Stream.seekg(seekPos);
        std::vector<char> audio(file.size);
        m_Stream.read(audio.data(), file.size);
        if (static_cast<uint32_t>(m_Stream.gcount()) != file.size) {
            std::cerr << "  [" << i << "] short read for " << origName << "\n";
            continue;
        }

        // Write SS2 header + data
        SS2Header hdr{};
        hdr.type       = 0x64685353; // 'SShd'
        hdr.one        = 0x18;
        hdr.two        = 0x10;
        hdr.frequency  = file.frequency;
        hdr.channels   = 1;
        hdr.interleave = 0;
        hdr.zero       = 0;
        hdr.max        = 0xFFFFFFFF;
        hdr.three      = 0x64625353; // 'SSbd'
        hdr.size       = file.size;

        {
            std::ofstream out(ss2Path, std::ios::binary);
            if (!out) {
                std::cerr << "  [" << i << "] cannot create " << ss2Path << "\n";
                continue;
            }
            out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            out.write(audio.data(), file.size);
        }

        std::cout << "  [" << i << "] " << folderName << "/" << ss2Path.filename().string()
                  << "  (" << file.size << " bytes, " << file.frequency << " Hz)\n";

        if (extractOnly) continue;

        // Convert with vgmstream
#ifdef _WIN32
        std::string cmd = "\"" + pathToVGMStream + "\" -o \"" + wavPath.string()
                        + "\" -l 1.0 -f 0.0 \"" + ss2Path.string() + "\"";
        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');
        if (CreateProcessA(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                           0, nullptr, nullptr, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            std::cerr << "  Failed to run vgmstream for " << ss2Path << "\n";
        }
#else
        // Linux / macOS
        std::string cmd = pathToVGMStream + " -o \"" + wavPath.string()
                        + "\" -l 1.0 -f 0.0 \"" + ss2Path.string() + "\"";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "  vgmstream failed for " << ss2Path << " (code " << ret << ")\n";
        }
#endif

        if (cleanUp && fs::exists(wavPath)) {
            fs::remove(ss2Path);
        }
    }

    std::cout << "Done.\n";
}
