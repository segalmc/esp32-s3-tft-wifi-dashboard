#pragma once

#include "HttpTelemetryJournal.h"
#include <LittleFS.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace http_telemetry {
class LittleFsJournalStore final : public JournalStore {
public:
    ~LittleFsJournalStore() override { close_read(); }
    // Never format an existing partition. A failed mount disables telemetry
    // until storage can be provisioned by a separately reviewed procedure.
    bool begin() { return LittleFS.begin(false); }
    size_t total_bytes() const { return LittleFS.totalBytes(); }
    size_t used_bytes() const { return LittleFS.usedBytes(); }
    bool has_any() override {
        DIR* dir = opendir("/littlefs");
        if (!dir) return true;
        bool found = false;
        for (dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
            if (strncmp(entry->d_name, "hwpq", 4) == 0) { found = true; break; }
        }
        closedir(dir);
        return found;
    }
    bool exists(uint32_t segment) override {
        char path[40]; segment_path(segment, path);
        struct stat info{};
        return stat(path, &info) == 0;
    }
    bool size(uint32_t segment, size_t& bytes) override {
        char path[40]; segment_path(segment, path);
        struct stat info{};
        if (stat(path, &info) != 0 || info.st_size < 0) return false;
        bytes = static_cast<size_t>(info.st_size);
        return true;
    }
    bool read(uint32_t segment, size_t offset, void* data, size_t bytes) override {
        if (read_segment_ != segment) {
            close_read();
            char path[40]; segment_path(segment, path);
            read_fd_ = open(path, O_RDONLY);
            if (read_fd_ < 0) return false;
            read_segment_ = segment;
        }
        return lseek(read_fd_, offset, SEEK_SET) == static_cast<off_t>(offset) &&
               exact_read(read_fd_, data, bytes);
    }
    bool create(uint32_t segment) override {
        char path[40]; segment_path(segment, path);
        const int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) return false;
        const bool ok = fsync(fd) == 0;
        close(fd);
        return ok;
    }
    bool append(uint32_t segment, const void* data, size_t bytes) override {
        if (total_bytes() < used_bytes() + bytes + 8192) return false;
        close_read();
        char path[40]; segment_path(segment, path);
        const int fd = open(path, O_WRONLY | O_APPEND);
        if (fd < 0) return false;
        const bool ok = exact_write(fd, data, bytes) && fsync(fd) == 0;
        close(fd);
        return ok;
    }
    bool truncate(uint32_t segment, size_t bytes) override {
        close_read();
        char path[40]; segment_path(segment, path);
        const int fd = open(path, O_WRONLY);
        if (fd < 0) return false;
        const bool ok = ftruncate(fd, bytes) == 0 && fsync(fd) == 0;
        close(fd);
        return ok;
    }
    bool remove(uint32_t segment) override {
        close_read();
        char path[40]; segment_path(segment, path);
        return unlink(path) == 0;
    }
    bool read_meta(unsigned slot, void* data, size_t bytes) override {
        char path[40]; meta_path(slot, path);
        const int fd = open(path, O_RDONLY);
        if (fd < 0) return false;
        struct stat info{};
        const bool ok = fstat(fd, &info) == 0 &&
                        info.st_size == static_cast<off_t>(bytes) &&
                        exact_read(fd, data, bytes);
        close(fd);
        return ok;
    }
    bool write_meta(unsigned slot, const void* data, size_t bytes) override {
        char path[40]; meta_path(slot, path);
        const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0) return false;
        const bool ok = exact_write(fd, data, bytes) && fsync(fd) == 0;
        close(fd);
        return ok;
    }
private:
    void close_read() {
        if (read_fd_ >= 0) close(read_fd_);
        read_fd_ = -1; read_segment_ = 0;
    }
    int read_fd_ = -1;
    uint32_t read_segment_ = 0;
    static void segment_path(uint32_t segment, char* path) {
        snprintf(path, 40, "/littlefs/hwpq%08lx.bin",
                 static_cast<unsigned long>(segment));
    }
    static void meta_path(unsigned slot, char* path) {
        snprintf(path, 40, "/littlefs/hwpqmeta%u", slot & 1U);
    }
    static bool exact_write(int fd, const void* data, size_t bytes) {
        const auto* source = static_cast<const uint8_t*>(data);
        size_t offset = 0;
        while (offset < bytes) {
            const ssize_t wrote = write(fd, source + offset, bytes - offset);
            if (wrote <= 0) return false;
            offset += static_cast<size_t>(wrote);
        }
        return true;
    }
    static bool exact_read(int fd, void* data, size_t bytes) {
        auto* target = static_cast<uint8_t*>(data);
        size_t offset = 0;
        while (offset < bytes) {
            const ssize_t got = ::read(fd, target + offset, bytes - offset);
            if (got <= 0) return false;
            offset += static_cast<size_t>(got);
        }
        return true;
    }
};
} // namespace http_telemetry
