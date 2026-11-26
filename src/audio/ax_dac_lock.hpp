#pragma once

#if !defined(_WIN32)
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#else
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace flac_ax::audio {

class ScopedDacLock {
public:
    ScopedDacLock() noexcept = default;

    ScopedDacLock(const ScopedDacLock&) = delete;
    ScopedDacLock& operator=(const ScopedDacLock&) = delete;

    ScopedDacLock(ScopedDacLock&& other) noexcept
#if !defined(_WIN32)
        : fd_(other.fd_), last_errno_(other.last_errno_), locked_(other.locked_) {
        other.fd_ = -1;
        other.last_errno_ = 0;
        other.locked_ = false;
    }
#else
        : mutex_(other.mutex_), last_errno_(other.last_errno_), locked_(other.locked_) {
        other.mutex_ = nullptr;
        other.last_errno_ = 0;
        other.locked_ = false;
    }
#endif

    ScopedDacLock& operator=(ScopedDacLock&& other) noexcept {
        if (this != &other) {
            Release();
#if !defined(_WIN32)
            fd_ = other.fd_;
            last_errno_ = other.last_errno_;
            locked_ = other.locked_;
            other.fd_ = -1;
            other.last_errno_ = 0;
            other.locked_ = false;
#else
            mutex_ = other.mutex_;
            last_errno_ = other.last_errno_;
            locked_ = other.locked_;
            other.mutex_ = nullptr;
            other.last_errno_ = 0;
            other.locked_ = false;
#endif
        }
        return *this;
    }

    ~ScopedDacLock() noexcept {
        Release();
    }

    bool Acquire(const char* lock_path = "/run/lock/ax-dac.lock",
                 bool non_blocking = false) noexcept {
        Release();
        last_errno_ = 0;
#if !defined(_WIN32)
        if (!lock_path || lock_path[0] == '\0') {
            last_errno_ = EINVAL;
            return false;
        }

        int fd = -1;
        while (true) {
            fd = ::open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
            if (fd >= 0) {
                break;
            }
            if (errno != EINTR) {
                last_errno_ = errno;
                return false;
            }
        }

        const int op = non_blocking ? (LOCK_EX | LOCK_NB) : LOCK_EX;
        while (::flock(fd, op) != 0) {
            if (errno == EINTR) {
                continue;
            }
            last_errno_ = errno;
            while (::close(fd) != 0 && errno == EINTR) {
            }
            return false;
        }

        fd_ = fd;
        locked_ = true;
        return true;
#else
        (void)lock_path;
        HANDLE h = ::CreateMutexW(nullptr, FALSE, L"Global\\AX_DAC_LOCK");
        if (!h) {
            last_errno_ = static_cast<int>(::GetLastError());
            return false;
        }
        const DWORD timeout_ms = non_blocking ? 0UL : INFINITE;
        const DWORD wait_res = ::WaitForSingleObject(h, timeout_ms);
        if (wait_res == WAIT_OBJECT_0 || wait_res == WAIT_ABANDONED) {
            mutex_ = h;
            locked_ = true;
            return true;
        }
        last_errno_ = (wait_res == WAIT_TIMEOUT)
            ? static_cast<int>(ERROR_TIMEOUT)
            : static_cast<int>(::GetLastError());
        ::CloseHandle(h);
        return false;
#endif
    }

    void Release() noexcept {
#if !defined(_WIN32)
        if (fd_ >= 0) {
            if (locked_) {
                while (::flock(fd_, LOCK_UN) != 0 && errno == EINTR) {
                }
                locked_ = false;
            }
            while (::close(fd_) != 0 && errno == EINTR) {
            }
            fd_ = -1;
        }
#else
        if (mutex_ != nullptr) {
            if (locked_) {
                ::ReleaseMutex(mutex_);
                locked_ = false;
            }
            ::CloseHandle(mutex_);
            mutex_ = nullptr;
        }
#endif
    }

    bool is_locked() const noexcept { return locked_; }
    int last_error() const noexcept { return last_errno_; }

private:
#if !defined(_WIN32)
    int fd_{-1};
#else
    HANDLE mutex_{nullptr};
#endif
    int last_errno_{0};
    bool locked_{false};
};

} // namespace flac_ax::audio
