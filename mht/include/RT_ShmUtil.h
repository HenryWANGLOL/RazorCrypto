#pragma once
#define _FILE_OFFSET_BITS 64

#include <string>
#include <fstream>
#include <stdexcept>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

namespace mht_rt {

class share_memory_util {
public:
    inline static share_memory_util* get_instance() {
        static share_memory_util util;
        return &util;
    }

    static void* load_page_buffer(const std::string& path, long long size, bool isCreator, bool quickMode, bool is_shm_open, bool isWriter = true) {
#if defined(__linux__) || defined(__APPLE__)
        int fd = -1;
        if (!is_shm_open) {
            fd = open(path.c_str(), isCreator ? (O_RDWR | O_CREAT) : O_RDWR, 0666);
        }
        else {
            fd = shm_open(path.c_str(), isCreator ? (O_CREAT | O_RDWR) : O_RDWR, 0666);
        }

        if (fd < 0 || (isCreator && ftruncate(fd, size) < 0)) {
            throw std::runtime_error("open or ftruncate failed");
        }

        if (isCreator && fchmod(fd, 0666) == -1) {
            throw std::runtime_error("fchmod failed");
        }

        void* buffer = mmap(nullptr, size, isWriter ? (PROT_READ | PROT_WRITE) : PROT_READ, MAP_SHARED, fd, 0);

        if (buffer == MAP_FAILED) {
            throw std::runtime_error("mmap failed");
        }

        if (!quickMode && (madvise(buffer, size, MADV_RANDOM) != 0 || mlock(buffer, size) != 0)) {
            munmap(buffer, size);
            throw std::runtime_error("madvise/mlock failed");
        }

        close(fd);
        return buffer;
#else
        DWORD access = (isCreator || isWriter) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
        HANDLE hFile = CreateFileA(path.c_str(), access,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            (isCreator ? OPEN_ALWAYS : OPEN_EXISTING),
            FILE_ATTRIBUTE_NORMAL, NULL);

        if (hFile == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("CreateFileA failed");
        }

        HANDLE hMap = CreateFileMapping(hFile, NULL,
            (isCreator || isWriter) ? PAGE_READWRITE : PAGE_READONLY,
            0, size, NULL);
        if (!hMap) {
            CloseHandle(hFile);
            throw std::runtime_error("CreateFileMapping failed");
        }

        void* buffer = MapViewOfFile(hMap,
            (isCreator || isWriter) ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ,
            0, 0, size);
        CloseHandle(hFile);
        CloseHandle(hMap);
        return buffer;
#endif
    }

    static void release_page_buffer(void* buffer, long long size, bool quickMode) {
#if defined(__linux__) || defined(__APPLE__)
        if (!quickMode && munlock(buffer, size) != 0) {
            throw std::runtime_error("munlock failed");
        }
        if (munmap(buffer, size) != 0) {
            throw std::runtime_error("munmap failed");
        }
#else
        UnmapViewOfFile(buffer);
#endif
    }

    static bool file_exists(const std::string& filename) {
#if defined(__linux__) || defined(__APPLE__)
        return access(filename.c_str(), F_OK) == 0;
#else
        std::ifstream f(filename.c_str());
        return f.good();
#endif
    }
};
};