#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>

struct Header {
    char id[32];          // "LithTech PSX Sound Data 1.20\r\n\x1a\0"
    uint32_t dummy;
    uint32_t folders;
    uint32_t files;
    uint32_t dummy2;
    uint32_t dummy3;
};

struct FileEntry {
    char filename[0x20];
    uint32_t zero;
    uint32_t offset;
    uint32_t size;
    uint32_t frequency;
    uint32_t zero2;
    uint32_t size2;
    uint32_t dummy[4];
};

struct FolderEntry {
    char foldername[32];
    uint32_t subfolders;      // number of child folders
    uint32_t files;           // number of files in this folder
    uint32_t priority;        // higher = closer to root
    uint32_t fileStartIndex;  // first file index belonging to this folder
};

struct SS2Header {
    uint32_t type;       // 'SShd'
    uint32_t one;        // 0x18
    uint32_t two;        // 0x10
    uint32_t frequency;
    uint32_t channels;
    uint32_t interleave;
    uint32_t zero;
    uint32_t max;
    uint32_t three;      // 'SSbd'
    uint32_t size;
};

class LPS
{
public:
    LPS();
    ~LPS();

    bool Open(const std::string& filename);
    bool Close();

    // extractOnly = true  -> only write .ss2
    // cleanUp    = true  -> delete .ss2 after successful conversion
    void Extract(const std::string& pathToVGMStream, bool extractOnly, bool cleanUp);

private:
    // Returns a clean folder name (printable chars only, no path separators)
    static std::string CleanFolderName(const char* raw, size_t maxLen);

    Header m_Header{};
    std::vector<FileEntry> m_Files;
    std::vector<FolderEntry> m_Folders;

    std::ifstream m_Stream;
    bool m_Closed = false;
    uint32_t m_BaseOffset = 0;
};
