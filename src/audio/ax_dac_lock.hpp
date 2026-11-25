#pragma once

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include <cstdio>

namespace flac_ax::audio {

class ScopedDacLock {
public:
    ScopedDacLock() noexcept = default;

    ScopedDacLock(const ScopedDacLock&) = delete;
    ScopedDacLock& operator=(const ScopedDacLock&) = delete;

    ScopedDacLock(ScopedDacLock&& other) noexcept
        : fd_(other.fd_), locked_(other.locked_) {
        other.fd_ = -1;
        other.locked_ = false;
    }

    ScopedDacLock& operator=(ScopedDacLock&& other) noexcept {
        if (this != &other) {
            Release();
            fd_ = other.fd_;
            locked_ = other.locked_;
            other.fd_ = -1;
            other.locked_ = false;
        }
        return *this;
    }

    ~ScopedDacLock() noexcept {
        Release();
    }

    bool Acquire(const char* lock_path = "/run/lock/ax-dac.lock",
                 bool non_blocking = false) noexcept {
        Release();
#if !defined(_WIN32)
        if (!lock_path) {
            return false;
        }
        const int fd = ::open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd < 0) {
            return false;
        }
        const int op = non_blocking ? (LOCK_EX | LOCK_NB) : LOCK_EX;
        if (::flock(fd, op) != 0) {
            ::close(fd);
            return false;
        }
        fd_ = fd;
        locked_ = true;
        return true;
#else
        (void)lock_path;
        (void)non_blocking;
        locked_ = true;
        return true;
#endif
    }

    void Release() noexcept {
#if !defined(_WIN32)
        if (fd_ >= 0) {
            if (locked_) {
                (void)::flock(fd_, LOCK_UN);
                locked_ = false;
            }
            (void)::close(fd_);
            fd_ = -1;
        }
#else
        locked_ = false;
#endif
    }

    bool is_locked() const noexcept { return locked_; }

private:
    int fd_{-1};
    bool locked_{false};
};

} // namespace flac_ax::audio
