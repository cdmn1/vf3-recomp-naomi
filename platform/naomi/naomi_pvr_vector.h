#ifndef VF3_NAOMI_PVR_VECTOR_H
#define VF3_NAOMI_PVR_VECTOR_H

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <dc/pvr.h>

/* Model 3 texture RAM is 8 MiB. A retail-mode result from KOS can occur in
 * Naomi emulation even though the Naomi PVR has 16 MiB. In that case KOS's
 * allocator manages only the lower 8 MiB; reserve the upper half exclusively
 * for this buffer. When KOS correctly identifies Naomi, use its normal pool. */
template <typename T>
class NaomiPvrBuffer {
public:
    NaomiPvrBuffer() = default;
    ~NaomiPvrBuffer() { clear(); }

    NaomiPvrBuffer(const NaomiPvrBuffer &) = delete;
    NaomiPvrBuffer &operator=(const NaomiPvrBuffer &) = delete;

    NaomiPvrBuffer(NaomiPvrBuffer &&other) noexcept
        : data_(other.data_), size_(other.size_), owns_pvr_(other.owns_pvr_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.owns_pvr_ = false;
    }

    NaomiPvrBuffer &operator=(NaomiPvrBuffer &&other) noexcept
    {
        if (this != &other) {
            clear();
            data_ = other.data_;
            size_ = other.size_;
            owns_pvr_ = other.owns_pvr_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.owns_pvr_ = false;
        }
        return *this;
    }

    void assign(std::size_t count, const T &value)
    {
        if (count != size_) {
            clear();
            if (!count)
                return;

            const std::size_t bytes = count * sizeof(T);
            if (hardware_sys_mode(nullptr) == HW_TYPE_RETAIL) {
                constexpr std::size_t kNaomiUpperPvrHalf = 8u * 1024u * 1024u;
                if (bytes > kNaomiUpperPvrHalf)
                    return;
                data_ = reinterpret_cast<T *>(
                    static_cast<uintptr_t>(PVR_RAM_BASE) + kNaomiUpperPvrHalf);
                size_ = count;
                owns_pvr_ = false;
            } else {
                if (pvr_mem_available() < bytes)
                    return;
                data_ = static_cast<T *>(pvr_mem_malloc(bytes));
                if (!data_)
                    return;
                size_ = count;
                owns_pvr_ = true;
            }
        }
        std::fill(begin(), end(), value);
    }

    void clear()
    {
        if (data_ && owns_pvr_)
            pvr_mem_free(data_);
        data_ = nullptr;
        size_ = 0;
        owns_pvr_ = false;
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
    bool owns_pvr_ = false;
};

#endif
