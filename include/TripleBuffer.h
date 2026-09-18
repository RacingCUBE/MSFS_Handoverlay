#pragma once

#include <atomic>
#include <array>

/**
 * @brief Lock-free triple buffer for producer-consumer pattern
 *
 * Implements a lock-free triple buffering mechanism where:
 * - Producer (capture thread) can write without blocking
 * - Consumer (main thread) can read without blocking
 * - No mutexes or condition variables required
 * - Latency ≈ memory copy time (minimal overhead)
 *
 * The three buffers allow:
 * - One buffer being written by producer
 * - One buffer being read by consumer
 * - One buffer as swap space
 *
 * @tparam T Data type to buffer (typically FrameData)
 */
template<typename T>
class TripleBuffer {
public:
    /**
     * @brief Constructor - initializes buffer indices
     */
    TripleBuffer()
        : writeIndex(0)
        , readIndex(1)
    {}

    /**
     * @brief Producer: Write new data (non-blocking)
     *
     * Writes data to the next available buffer and atomically updates the write index.
     * This operation never blocks and always succeeds.
     *
     * @param data Data to write (will be copied)
     */
    void write(const T& data) {
        // Get next write buffer (rotate through 0, 1, 2)
        int nextWrite = (writeIndex.load(std::memory_order_relaxed) + 1) % 3;

        // Write data to buffer
        buffers[nextWrite] = data;

        // Atomically publish the new write index
        // memory_order_release ensures all writes complete before index update is visible
        writeIndex.store(nextWrite, std::memory_order_release);
    }

    /**
     * @brief Producer: Write new data with move semantics (non-blocking, more efficient)
     *
     * @param data Data to move (no copy)
     */
    void write(T&& data) {
        int nextWrite = (writeIndex.load(std::memory_order_relaxed) + 1) % 3;
        buffers[nextWrite] = std::move(data);
        writeIndex.store(nextWrite, std::memory_order_release);
    }

    /**
     * @brief Consumer: Read latest data (non-blocking)
     *
     * Reads the latest available data if new data has been written since last read.
     * Returns false if no new data is available.
     *
     * @param data Output parameter to receive the data
     * @return true if new data was read, false if no new data available
     */
    bool read(T& data) {
        // Get current write index (latest data)
        // memory_order_acquire ensures we see all writes that happened before writeIndex update
        int currentWrite = writeIndex.load(std::memory_order_acquire);

        // Get our current read index
        int currentRead = readIndex.load(std::memory_order_relaxed);

        // Check if there's new data
        if (currentWrite != currentRead) {
            // Copy data from buffer
            data = buffers[currentWrite];

            // Update read index to mark as consumed
            // memory_order_release ensures the data copy completes before index update
            readIndex.store(currentWrite, std::memory_order_release);

            return true;  // New data was read
        }

        return false;  // No new data
    }

    /**
     * @brief Consumer: Peek at latest data without marking as read (non-blocking)
     *
     * Useful for checking current state without consuming the data.
     *
     * @return Const reference to the latest buffer
     */
    const T& peek() const {
        int currentWrite = writeIndex.load(std::memory_order_acquire);
        return buffers[currentWrite];
    }

    /**
     * @brief Check if new data is available (non-blocking)
     *
     * @return true if write index differs from read index (new data available)
     */
    bool hasNewData() const {
        return writeIndex.load(std::memory_order_acquire) !=
               readIndex.load(std::memory_order_relaxed);
    }

private:
    std::array<T, 3> buffers;           ///< Three buffers for triple buffering
    std::atomic<int> writeIndex;         ///< Index of the buffer being written (producer)
    std::atomic<int> readIndex;          ///< Index of the buffer last read (consumer)
};
