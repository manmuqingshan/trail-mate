/**
 * @file ringbuf.h
 * @brief Fixed-size ring buffer template
 */

#pragma once

#include <cstddef>
#include <utility>

namespace sys
{

/**
 * @brief Fixed-size ring buffer
 * @tparam T Element type
 * @tparam N Buffer size
 */
template <typename T, size_t N>
class RingBuffer
{
  public:
    RingBuffer() : head_(0), tail_(0), count_(0)
    {
        static_assert(N > 0, "RingBuffer size must be > 0");
    }

    /**
     * @brief Append an element to the buffer
     * @param item Item to append
     * @return true after appending; when full, the oldest element is dropped
     */
    bool append(const T& item, bool* dropped = nullptr)
    {
        return appendImpl(item, dropped);
    }

    bool append(T&& item, bool* dropped = nullptr)
    {
        return appendImpl(std::move(item), dropped);
    }

    bool popOldest(T* out)
    {
        if (!out || count_ == 0)
        {
            return false;
        }

        *out = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % N;
        count_--;
        return true;
    }

    bool pushDropOldest(const T& item, bool* dropped = nullptr)
    {
        return append(item, dropped);
    }

    bool pushDropOldest(T&& item, bool* dropped = nullptr)
    {
        return append(std::move(item), dropped);
    }

  private:
    template <typename U>
    bool appendImpl(U&& item, bool* dropped)
    {
        bool was_full = false;
        if (count_ >= N)
        {
            // Buffer full, overwrite oldest
            tail_ = (tail_ + 1) % N;
            count_--;
            was_full = true;
        }
        if (dropped)
        {
            *dropped = was_full;
        }
        buffer_[head_] = std::forward<U>(item);
        head_ = (head_ + 1) % N;
        count_++;
        return true;
    }

  public:
    /**
     * @brief Get element at index (0 = oldest, count-1 = newest)
     * @param index Index
     * @return Pointer to element, or nullptr if index out of range
     */
    const T* get(size_t index) const
    {
        if (index >= count_)
        {
            return nullptr;
        }
        size_t pos = (tail_ + index) % N;
        return &buffer_[pos];
    }

    /**
     * @brief Get mutable element at index (0 = oldest, count-1 = newest)
     * @param index Index
     * @return Pointer to element, or nullptr if index out of range
     */
    T* get(size_t index)
    {
        if (index >= count_)
        {
            return nullptr;
        }
        size_t pos = (tail_ + index) % N;
        return &buffer_[pos];
    }

    /**
     * @brief Get the newest element
     * @return Pointer to newest element, or nullptr if empty
     */
    const T* getNewest() const
    {
        if (count_ == 0)
        {
            return nullptr;
        }
        size_t pos = (head_ + N - 1) % N;
        return &buffer_[pos];
    }

    /**
     * @brief Get number of elements in buffer
     */
    size_t count() const
    {
        return count_;
    }

    size_t size() const
    {
        return count_;
    }

    /**
     * @brief Check if buffer is full
     */
    bool isFull() const
    {
        return count_ >= N;
    }

    /**
     * @brief Check if buffer is empty
     */
    bool isEmpty() const
    {
        return count_ == 0;
    }

    bool empty() const
    {
        return count_ == 0;
    }

    /**
     * @brief Clear the buffer
     */
    void clear()
    {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    /**
     * @brief Get all elements as a vector (for iteration)
     * Note: This creates a copy, use with caution on memory-constrained systems
     */
    void getAll(T* out, size_t max_count) const
    {
        size_t copy_count = (max_count < count_) ? max_count : count_;
        for (size_t i = 0; i < copy_count; i++)
        {
            size_t pos = (tail_ + i) % N;
            out[i] = buffer_[pos];
        }
    }

  private:
    T buffer_[N];
    size_t head_;  // Next write position
    size_t tail_;  // Oldest element position
    size_t count_; // Current element count
};

} // namespace sys
