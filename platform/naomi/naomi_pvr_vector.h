#ifndef VF3_NAOMI_PVR_VECTOR_H
#define VF3_NAOMI_PVR_VECTOR_H

#include <algorithm>
#include <cstddef>
#include <dc/pvr.h>

/* A small vector-like owner for large Model 3 texture memory. Naomi's PVR2
 * exposes 16 MiB of SH-4-addressable VRAM; keeping the 8 MiB Model 3 texture
 * RAM there releases scarce 32 MiB main RAM for the PPC and board memories. */
template <typename T>
class NaomiPvrBuffer {
public:
    NaomiPvrBuffer() = default;
    ~NaomiPvrBuffer() { clear(); }

    NaomiPvrBuffer(const NaomiPvrBuffer &) = delete;
    NaomiPvrBuffer &operator=(const NaomiPvrBuffer &) = delete;

    NaomiPvrBuffer(NaomiPvrBuffer &&other) noexcept
        : data_(other.data_), size_(other.size_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    NaomiPvrBuffer &operator=(NaomiPvrBuffer &&other) noexcept
    {
        if (this != &other) {
            clear();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void assign(std::size_t count, const T &value)
    {
        if (count != size_) {
            clear();
            if (!count)
                return;
            if (pvr_mem_available() < count * sizeof(T))
                return;
            data_ = static_cast<T *>(pvr_mem_malloc(count * sizeof(T)));
            if (!data_)
                return;
            size_ = count;
        }
        std::fill(begin(), end(), value);
    }

    void clear()
    {
        if (data_)
            pvr_mem_free(data_);
        data_ = nullptr;
        size_ = 0;
    }

    T *data() { return data_; }
    const T *data() const { return data_; }
    T *begin() { return data_; }
    const T *begin() const { return data_; }
    T *end() { return data_ ? data_ + size_ : data_; }
    const T *end() const { return data_ ? data_ + size_ : data_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    T &operator[](std::size_t index) { return data_[index]; }
    const T &operator[](std::size_t index) const { return data_[index]; }

private:
    T *data_ = nullptr;
    std::size_t size_ = 0;
};

#endif
