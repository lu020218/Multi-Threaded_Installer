#pragma once

#include <cstddef>
#include <cstdint>

namespace MultiThreadedInstaller {

// Stable, non-cryptographic 64-bit content fingerprint (FNV-1a).
//
// Used by the incremental install path so the packager and the installer
// agree on a per-file "content hash". The hash covers file *content* only
// (never the path or timestamps), so identical content always yields an
// identical hash regardless of where the file lives. Determinism across the
// packager and installer builds is the only requirement, which FNV-1a meets
// without pulling in an external dependency.
class ContentHasher {
public:
    void update(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            hash_ ^= static_cast<uint64_t>(bytes[i]);
            hash_ *= kPrime;
        }
    }

    uint64_t finalize() const { return hash_; }

private:
    static constexpr uint64_t kOffsetBasis = 14695981039346656037ULL;
    static constexpr uint64_t kPrime = 1099511628211ULL;
    uint64_t hash_ = kOffsetBasis;
};

// Convenience one-shot helper for callers that already hold the full buffer.
inline uint64_t ComputeContentHash64(const void* data, size_t size) {
    ContentHasher hasher;
    hasher.update(data, size);
    return hasher.finalize();
}

} // namespace MultiThreadedInstaller
