#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// RAII wrapper for memory-mapped files
class MappedFile {
public:
    MappedFile(const std::string& path) : fd_(-1), data_(nullptr), size_(0) {
        fd_ = open(path.c_str(), O_RDONLY);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open file: " + path);
        }

        struct stat sb;
        if (fstat(fd_, &sb) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to stat file: " + path);
        }
        size_ = sb.st_size;

        data_ = static_cast<const uint8_t*>(
            mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0)
        );
        
        if (data_ == MAP_FAILED) {
            close(fd_);
            throw std::runtime_error("Failed to mmap file: " + path);
        }

        // Advise kernel we'll access sequentially
        madvise(const_cast<uint8_t*>(data_), size_, MADV_SEQUENTIAL);
    }

    ~MappedFile() {
        if (data_ && data_ != MAP_FAILED) {
            munmap(const_cast<uint8_t*>(data_), size_);
        }
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    // Delete copy/move for simplicity
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    int fd_;
    const uint8_t* data_;
    size_t size_;
};
