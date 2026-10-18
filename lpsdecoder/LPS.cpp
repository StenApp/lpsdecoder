#include "LPS.h"
#include <iostream>
#include <map>
#include <set>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#else
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define PATH_SEP "/"
#endif

// ---- small path helpers (no std::filesystem needed) ----
static bool DirExists(const std::string& path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool FileExists(const std::string& path)
{
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES) && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

static bool CreateDirRecursive(const std::string& path)
{
    if (path.empty() || DirExists(path)) return true;

    // create parent first
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        if (!CreateDirRecursive(path.substr(0, pos))) return false;
    }

#ifdef _WIN32
    return _mkdir(path.c_str()) == 0 || DirExists(path);
#else
    return mkdir(path.c_str(), 0755) == 0 || DirExists(path);
#endif
}

static bool RemoveFile(const std::string& path)
{
#ifdef _WIN32
    return DeleteFileA(path.c_str()) != 0;
#else
    return unlink(path.c_str()) == 0;
#endif
}

static std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + PATH_SEP + b;
}

// -------------------------------------------------------

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

    m_Stream.read(reinterpret_cast<char*>(&m_Header), sizeof(m_Header));
    if (m_Stream.gcount() != static_cast<std::streamsize>(sizeof(m_Header))) {
        std::cerr << "Failed to read header\n";
        return false;
    }

    if (std::strncmp(m_Header.id, "LithTech PSX Sound Data 1.20", 28) != 0) {
        std::cerr << "Not a valid LithTech PSX Sound Data file\n";
        return false;
    }

    std::cout << "Folders: " << m_Header.folders << "  Files: " << m_Header.files << "\n";

    m_Files.resize(m_Header.files);
    for (uint32_t i = 0; i < m_Header.files; ++i) {
        m_Stream.read(reinterpret_cast<char*>(&m_Files[i]), sizeof(FileEntry));
    }

    m_Folders.resize(m_Header.folders);
    for (uint32_t i = 0; i < m_Header.folders; ++i) {
        m_Stream.read(reinterpret_cast<char*>(&m_Folders[i]), sizeof(FolderEntry));
    }

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
        if (c >= 32 && c < 127 && c != '/' && c != '\\' && c != ':' && c != '*') {
            name += static_cast<char>(c);
        } else {
            break;
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    if (name.empty()) name = "unknown";
    return name;
}

void LPS::Extract(const std::string& pathToVGMStream, bool extractOnly, bool cleanUp,
                 const std::string& outputRoot)
{
    uint32_t okCount = 0;
    uint32_t skipCount = 0;
    uint32_t failCount = 0;

    // fileIndex -> folder name
    std::map<uint32_t, std::string> fileToFolder;

    for (size_t fi = 0; fi < m_Folders.size(); ++fi) {
        const FolderEntry& folder = m_Folders[fi];
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
            fileToFolder[i] = folderName;
        }
    }

    for (uint32_t i = 0; i < m_Header.files; ++i) {
        const FileEntry& file = m_Files[i];

        if (file.size == 0) {
            std::cout << "  [" << i << "] SKIP zero-size\n";
            ++skipCount;
            continue;
        }

        std::string folderName = "unassigned";
        auto it = fileToFolder.find(i);
        if (it != fileToFolder.end()) {
            folderName = it->second;
        }

        std::string origName(file.filename, strnlen(file.filename, 0x20));
        std::string stem = origName;
        size_t dot = stem.find_last_of('.');
        if (dot != std::string::npos) stem = stem.substr(0, dot);
        if (stem.empty()) stem = "unnamed_" + std::to_string(i);

        std::string outDir = JoinPath(outputRoot, folderName);
        CreateDirRecursive(outDir);

        std::string ss2Path = JoinPath(outDir, stem + ".ss2");
        std::string wavPath = JoinPath(outDir, stem + ".wav");

        int suffix = 1;
        while (FileExists(ss2Path) || FileExists(wavPath)) {
            ss2Path = JoinPath(outDir, stem + "_" + std::to_string(suffix) + ".ss2");
            wavPath = JoinPath(outDir, stem + "_" + std::to_string(suffix) + ".wav");
            ++suffix;
        }

        uint32_t seekPos = file.offset + m_BaseOffset;
        m_Stream.seekg(seekPos);
        std::vector<char> audio(file.size);
        m_Stream.read(audio.data(), file.size);
        if (static_cast<uint32_t>(m_Stream.gcount()) != file.size) {
            std::cerr << "  [" << i << "] short read for " << origName << "\n";
            ++failCount;
            continue;
        }

        SS2Header hdr = {};
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
            std::ofstream out(ss2Path.c_str(), std::ios::binary);
            if (!out) {
                std::cerr << "  [" << i << "] cannot create " << ss2Path << "\n";
                ++failCount;
                continue;
            }
            out.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
            out.write(audio.data(), file.size);
        }

        // filename only for display
        size_t slash = ss2Path.find_last_of("/\\");
        std::string displayName = (slash != std::string::npos) ? ss2Path.substr(slash + 1) : ss2Path;

        std::cout << "  [" << i << "] " << folderName << "/" << displayName
                  << "  (" << file.size << " bytes, " << file.frequency << " Hz)\n";
        ++okCount;

        if (extractOnly) continue;

#ifdef _WIN32
        std::string cmd = "\"" + pathToVGMStream + "\" -o \"" + wavPath
                        + "\" -l 1.0 -f 0.0 \"" + ss2Path + "\"";
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        ZeroMemory(&pi, sizeof(pi));
        si.cb = sizeof(si);
        std::vector<char> cmdBuf(cmd.begin(), cmd.end());
        cmdBuf.push_back('\0');
        if (CreateProcessA(NULL, &cmdBuf[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            std::cerr << "  Failed to run vgmstream for " << ss2Path << "\n";
        }
#else
        std::string cmd = pathToVGMStream + " -o \"" + wavPath
                        + "\" -l 1.0 -f 0.0 \"" + ss2Path + "\"";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "  vgmstream failed for " << ss2Path << " (code " << ret << ")\n";
        }
#endif

        if (cleanUp && FileExists(wavPath)) {
            RemoveFile(ss2Path);
        }
    }

    std::cout << "\n========== Summary ==========\n";
    std::cout << "  Total entries : " << m_Header.files << "\n";
    std::cout << "  Extracted OK  : " << okCount << "\n";
    std::cout << "  Skipped       : " << skipCount << "\n";
    std::cout << "  Failed        : " << failCount << "\n";
    if (m_Header.files > 0) {
        std::cout << "  Success rate  : " << (okCount * 100 / m_Header.files) << "%\n";
    }
    std::cout << "=============================\n";
}

