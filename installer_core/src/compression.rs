//! Compression and decompression module.
//!
//! Supports Zstd and LZMA compression algorithms with configurable compression levels
//! and CRC32 checksum verification.
//!
//! # Requirements
//! - 2.4: Default Zstd compression with level 3
//! - 2.5: Support for LZMA as alternative algorithm
//! - 3.5: Decompression using algorithm specified in block entry
//! - 3.6: Checksum verification after decompression

use installer_shared::{CompressionAlgorithm, InstallerError, Result};
use std::io::Cursor;
use tracing::{debug, trace};

/// Default Zstd compression level (balanced between speed and ratio).
pub const DEFAULT_ZSTD_LEVEL: i32 = 3;

/// Minimum valid Zstd compression level.
pub const MIN_ZSTD_LEVEL: i32 = 1;

/// Maximum valid Zstd compression level.
pub const MAX_ZSTD_LEVEL: i32 = 22;

/// Result of a compression operation including checksum.
#[derive(Debug, Clone)]
pub struct CompressionResult {
    /// Compressed data
    pub data: Vec<u8>,
    /// CRC32 checksum of the compressed data
    pub checksum: u32,
    /// Original (uncompressed) size
    pub original_size: u64,
    /// Compressed size
    pub compressed_size: u64,
}

/// Result of a decompression operation.
#[derive(Debug, Clone)]
pub struct DecompressionResult {
    /// Decompressed data
    pub data: Vec<u8>,
    /// CRC32 checksum of the decompressed data
    pub checksum: u32,
}

/// Compress data using the specified algorithm and return result with checksum.
///
/// # Arguments
/// * `data` - Data to compress
/// * `algorithm` - Compression algorithm to use
/// * `level` - Compression level (only used for Zstd, 1-22)
///
/// # Returns
/// CompressionResult containing compressed data and CRC32 checksum
pub fn compress_with_checksum(
    data: &[u8],
    algorithm: CompressionAlgorithm,
    level: i32,
) -> Result<CompressionResult> {
    let original_size = data.len() as u64;
    
    let compressed = match algorithm {
        CompressionAlgorithm::Zstd => compress_zstd(data, level)?,
        CompressionAlgorithm::Lzma => compress_lzma(data)?,
    };
    
    let checksum = calculate_crc32(&compressed);
    let compressed_size = compressed.len() as u64;
    
    debug!(
        algorithm = ?algorithm,
        original_size,
        compressed_size,
        ratio = format!("{:.2}%", (compressed_size as f64 / original_size as f64) * 100.0),
        "Compression complete"
    );
    
    Ok(CompressionResult {
        data: compressed,
        checksum,
        original_size,
        compressed_size,
    })
}

/// Compress data using the specified algorithm.
///
/// # Arguments
/// * `data` - Data to compress
/// * `algorithm` - Compression algorithm to use
/// * `level` - Compression level (only used for Zstd)
pub fn compress(data: &[u8], algorithm: CompressionAlgorithm, level: i32) -> Result<Vec<u8>> {
    match algorithm {
        CompressionAlgorithm::Zstd => compress_zstd(data, level),
        CompressionAlgorithm::Lzma => compress_lzma(data),
    }
}

/// Decompress data and verify checksum.
///
/// # Arguments
/// * `data` - Compressed data
/// * `algorithm` - Compression algorithm used
/// * `expected_checksum` - Expected CRC32 checksum of the compressed data
///
/// # Returns
/// DecompressionResult containing decompressed data and its checksum
///
/// # Errors
/// Returns ChecksumMismatch if the compressed data checksum doesn't match expected
pub fn decompress_with_checksum(
    data: &[u8],
    algorithm: CompressionAlgorithm,
    expected_checksum: u32,
) -> Result<DecompressionResult> {
    // Verify checksum of compressed data first
    verify_crc32(data, expected_checksum)?;
    
    let decompressed = match algorithm {
        CompressionAlgorithm::Zstd => decompress_zstd(data)?,
        CompressionAlgorithm::Lzma => decompress_lzma(data)?,
    };
    
    let checksum = calculate_crc32(&decompressed);
    
    debug!(
        algorithm = ?algorithm,
        compressed_size = data.len(),
        decompressed_size = decompressed.len(),
        "Decompression complete"
    );
    
    Ok(DecompressionResult {
        data: decompressed,
        checksum,
    })
}

/// Decompress data using the specified algorithm.
///
/// # Arguments
/// * `data` - Compressed data
/// * `algorithm` - Compression algorithm used
pub fn decompress(data: &[u8], algorithm: CompressionAlgorithm) -> Result<Vec<u8>> {
    match algorithm {
        CompressionAlgorithm::Zstd => decompress_zstd(data),
        CompressionAlgorithm::Lzma => decompress_lzma(data),
    }
}

// ============================================================================
// Zstd Implementation
// ============================================================================

/// Compress data using Zstd algorithm.
///
/// # Arguments
/// * `data` - Data to compress
/// * `level` - Compression level (1-22, default 3)
///
/// # Requirements
/// - 2.4: Default Zstd compression with level 3
fn compress_zstd(data: &[u8], level: i32) -> Result<Vec<u8>> {
    // Clamp level to valid range
    let level = level.clamp(MIN_ZSTD_LEVEL, MAX_ZSTD_LEVEL);
    
    trace!(level, data_size = data.len(), "Starting Zstd compression");
    
    zstd::encode_all(Cursor::new(data), level)
        .map_err(|e| InstallerError::Io(e))
}

/// Decompress Zstd compressed data.
///
/// # Arguments
/// * `data` - Zstd compressed data
///
/// # Requirements
/// - 3.5: Decompression using Zstd algorithm
fn decompress_zstd(data: &[u8]) -> Result<Vec<u8>> {
    trace!(compressed_size = data.len(), "Starting Zstd decompression");
    
    zstd::decode_all(Cursor::new(data))
        .map_err(|e| InstallerError::Decompression(format!("Zstd decompression failed: {}", e)))
}

// ============================================================================
// LZMA Implementation
// ============================================================================

/// Compress data using LZMA algorithm.
///
/// # Arguments
/// * `data` - Data to compress
///
/// # Requirements
/// - 2.5: LZMA as optional compression algorithm
fn compress_lzma(data: &[u8]) -> Result<Vec<u8>> {
    trace!(data_size = data.len(), "Starting LZMA compression");
    
    let mut output = Vec::new();
    lzma_rs::lzma_compress(&mut Cursor::new(data), &mut output)
        .map_err(|e| InstallerError::Io(std::io::Error::new(
            std::io::ErrorKind::Other,
            format!("LZMA compression failed: {}", e)
        )))?;
    
    Ok(output)
}

/// Decompress LZMA compressed data.
///
/// # Arguments
/// * `data` - LZMA compressed data
///
/// # Requirements
/// - 3.5: Decompression using LZMA algorithm
fn decompress_lzma(data: &[u8]) -> Result<Vec<u8>> {
    trace!(compressed_size = data.len(), "Starting LZMA decompression");
    
    let mut output = Vec::new();
    lzma_rs::lzma_decompress(&mut Cursor::new(data), &mut output)
        .map_err(|e| InstallerError::Decompression(format!("LZMA decompression failed: {}", e)))?;
    
    Ok(output)
}

// ============================================================================
// CRC32 Checksum Functions
// ============================================================================

/// Calculate CRC32 checksum of data.
///
/// # Arguments
/// * `data` - Data to calculate checksum for
///
/// # Returns
/// CRC32 checksum value
pub fn calculate_crc32(data: &[u8]) -> u32 {
    crc32fast::hash(data)
}

/// Verify CRC32 checksum matches expected value.
///
/// # Arguments
/// * `data` - Data to verify
/// * `expected` - Expected CRC32 checksum
///
/// # Returns
/// Ok(()) if checksum matches, Err(ChecksumMismatch) otherwise
///
/// # Requirements
/// - 3.6: Verify checksum matches expected value
/// - 8.2: Abort on checksum verification failure
pub fn verify_crc32(data: &[u8], expected: u32) -> Result<()> {
    let actual = calculate_crc32(data);
    if actual != expected {
        debug!(
            expected = format!("{:08x}", expected),
            actual = format!("{:08x}", actual),
            "Checksum mismatch detected"
        );
        return Err(InstallerError::ChecksumMismatch { expected, actual });
    }
    trace!(checksum = format!("{:08x}", actual), "Checksum verified");
    Ok(())
}

// ============================================================================
// Block Compression/Decompression
// ============================================================================

/// Compress a data block with full metadata.
///
/// This is the main entry point for block compression in the packager.
///
/// # Arguments
/// * `data` - Block data to compress
/// * `algorithm` - Compression algorithm to use
/// * `level` - Compression level (for Zstd)
///
/// # Returns
/// CompressionResult with compressed data and metadata
pub fn compress_block(
    data: &[u8],
    algorithm: CompressionAlgorithm,
    level: i32,
) -> Result<CompressionResult> {
    compress_with_checksum(data, algorithm, level)
}

/// Decompress a data block with checksum verification.
///
/// This is the main entry point for block decompression in the installer.
///
/// # Arguments
/// * `data` - Compressed block data
/// * `algorithm` - Compression algorithm used
/// * `expected_checksum` - Expected CRC32 checksum of compressed data
///
/// # Returns
/// Decompressed data as `Vec<u8>`
///
/// # Errors
/// Returns error if checksum verification fails or decompression fails
pub fn decompress_block(
    data: &[u8],
    algorithm: CompressionAlgorithm,
    expected_checksum: u32,
) -> Result<Vec<u8>> {
    let result = decompress_with_checksum(data, algorithm, expected_checksum)?;
    Ok(result.data)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_zstd_roundtrip() {
        let data = b"Hello, World! This is test data for compression.";
        let compressed = compress(data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        let decompressed = decompress(&compressed, CompressionAlgorithm::Zstd).unwrap();
        assert_eq!(data.as_slice(), decompressed.as_slice());
    }

    #[test]
    fn test_zstd_compression_levels() {
        let data = b"Test data for compression level testing. ".repeat(100);
        
        // Test minimum level
        let compressed_min = compress(&data, CompressionAlgorithm::Zstd, MIN_ZSTD_LEVEL).unwrap();
        let decompressed_min = decompress(&compressed_min, CompressionAlgorithm::Zstd).unwrap();
        assert_eq!(data.as_slice(), decompressed_min.as_slice());
        
        // Test maximum level
        let compressed_max = compress(&data, CompressionAlgorithm::Zstd, MAX_ZSTD_LEVEL).unwrap();
        let decompressed_max = decompress(&compressed_max, CompressionAlgorithm::Zstd).unwrap();
        assert_eq!(data.as_slice(), decompressed_max.as_slice());
        
        // Higher level should generally produce smaller output (for compressible data)
        assert!(compressed_max.len() <= compressed_min.len());
    }

    #[test]
    fn test_zstd_with_checksum() {
        let data = b"Test data for Zstd compression with checksum verification.";
        
        let result = compress_with_checksum(data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        assert!(result.compressed_size > 0);
        assert_eq!(result.original_size, data.len() as u64);
        
        // Verify checksum is correct
        let actual_checksum = calculate_crc32(&result.data);
        assert_eq!(result.checksum, actual_checksum);
        
        // Decompress with checksum verification
        let decompressed = decompress_with_checksum(&result.data, CompressionAlgorithm::Zstd, result.checksum).unwrap();
        assert_eq!(data.as_slice(), decompressed.data.as_slice());
    }

    #[test]
    fn test_lzma_roundtrip() {
        let data = b"Hello, World! This is test data for LZMA compression.";
        let compressed = compress(data, CompressionAlgorithm::Lzma, 0).unwrap();
        let decompressed = decompress(&compressed, CompressionAlgorithm::Lzma).unwrap();
        assert_eq!(data.as_slice(), decompressed.as_slice());
    }

    #[test]
    fn test_lzma_with_checksum() {
        let data = b"Test data for LZMA compression with checksum verification.";
        
        let result = compress_with_checksum(data, CompressionAlgorithm::Lzma, 0).unwrap();
        assert!(result.compressed_size > 0);
        assert_eq!(result.original_size, data.len() as u64);
        
        // Decompress with checksum verification
        let decompressed = decompress_with_checksum(&result.data, CompressionAlgorithm::Lzma, result.checksum).unwrap();
        assert_eq!(data.as_slice(), decompressed.data.as_slice());
    }

    #[test]
    fn test_crc32() {
        let data = b"Test data for CRC32";
        let checksum = calculate_crc32(data);
        assert!(verify_crc32(data, checksum).is_ok());
        assert!(verify_crc32(data, checksum + 1).is_err());
    }

    #[test]
    fn test_checksum_mismatch_error() {
        let data = b"Test data";
        let wrong_checksum = 0x12345678;
        
        let result = verify_crc32(data, wrong_checksum);
        assert!(result.is_err());
        
        match result {
            Err(InstallerError::ChecksumMismatch { expected, actual }) => {
                assert_eq!(expected, wrong_checksum);
                assert_eq!(actual, calculate_crc32(data));
            }
            _ => panic!("Expected ChecksumMismatch error"),
        }
    }

    #[test]
    fn test_decompress_with_wrong_checksum() {
        let data = b"Test data for decompression";
        let compressed = compress(data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        
        // Try to decompress with wrong checksum
        let wrong_checksum = 0xDEADBEEF;
        let result = decompress_with_checksum(&compressed, CompressionAlgorithm::Zstd, wrong_checksum);
        
        assert!(result.is_err());
        match result {
            Err(InstallerError::ChecksumMismatch { .. }) => {}
            _ => panic!("Expected ChecksumMismatch error"),
        }
    }

    #[test]
    fn test_block_compression() {
        let data = b"Block data for compression testing. ".repeat(50);
        
        let result = compress_block(&data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        
        let decompressed = decompress_block(&result.data, CompressionAlgorithm::Zstd, result.checksum).unwrap();
        assert_eq!(data.as_slice(), decompressed.as_slice());
    }

    #[test]
    fn test_empty_data_compression() {
        let data: &[u8] = b"";
        
        // Zstd handles empty data
        let compressed = compress(data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        let decompressed = decompress(&compressed, CompressionAlgorithm::Zstd).unwrap();
        assert_eq!(data, decompressed.as_slice());
    }

    #[test]
    fn test_large_data_compression() {
        // Test with 1MB of data
        let data: Vec<u8> = (0..1024 * 1024).map(|i| (i % 256) as u8).collect();
        
        let result = compress_with_checksum(&data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL).unwrap();
        let decompressed = decompress_with_checksum(&result.data, CompressionAlgorithm::Zstd, result.checksum).unwrap();
        
        assert_eq!(data, decompressed.data);
    }
}

// ============================================================================
// Property-Based Tests for Checksum Integrity Verification
// ============================================================================
// Property 4: Checksum Integrity Verification
// For any data block, if the data is modified, checksum verification should
// fail and abort the operation.
// **Validates: Requirements 3.6, 8.2**
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use proptest::prelude::*;

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// Property 4: Checksum Integrity Verification - CRC32 Determinism
        /// For any data, CRC32 calculation should be deterministic.
        /// **Validates: Requirements 3.6**
        #[test]
        fn prop_crc32_deterministic(data in prop::collection::vec(any::<u8>(), 0..1000)) {
            let checksum1 = calculate_crc32(&data);
            let checksum2 = calculate_crc32(&data);
            
            prop_assert_eq!(checksum1, checksum2, "CRC32 should be deterministic");
        }

        /// Property 4: Checksum Integrity Verification - Corruption Detection
        /// For any data block, if the data is modified, checksum verification should fail.
        /// **Validates: Requirements 3.6, 8.2**
        #[test]
        fn prop_checksum_detects_corruption(
            data in prop::collection::vec(any::<u8>(), 1..500),
            corrupt_pos in any::<usize>(),
            corrupt_value in any::<u8>()
        ) {
            let original_checksum = calculate_crc32(&data);
            prop_assert!(verify_crc32(&data, original_checksum).is_ok());
            
            let mut corrupted_data = data.clone();
            let pos = corrupt_pos % corrupted_data.len();
            
            if corrupted_data[pos] != corrupt_value {
                corrupted_data[pos] = corrupt_value;
                let result = verify_crc32(&corrupted_data, original_checksum);
                prop_assert!(result.is_err());
            }
        }

        /// Property 4: Checksum Integrity Verification - Wrong Checksum Rejection
        /// **Validates: Requirements 3.6, 8.2**
        #[test]
        fn prop_wrong_checksum_rejected(
            data in prop::collection::vec(any::<u8>(), 10..500),
            wrong_checksum in any::<u32>()
        ) {
            let result = compress_with_checksum(&data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL);
            prop_assert!(result.is_ok());
            let compression_result = result.unwrap();
            
            if wrong_checksum != compression_result.checksum {
                let decompress_result = decompress_with_checksum(
                    &compression_result.data,
                    CompressionAlgorithm::Zstd,
                    wrong_checksum
                );
                prop_assert!(decompress_result.is_err());
            }
        }

        /// Property 4: Checksum Integrity Verification - Block Decompression Abort
        /// **Validates: Requirements 3.6, 8.2**
        #[test]
        fn prop_block_decompression_aborts_on_checksum_mismatch(
            data in prop::collection::vec(any::<u8>(), 10..500),
            wrong_checksum in any::<u32>()
        ) {
            let result = compress_block(&data, CompressionAlgorithm::Zstd, DEFAULT_ZSTD_LEVEL);
            prop_assert!(result.is_ok());
            let compression_result = result.unwrap();
            
            if wrong_checksum != compression_result.checksum {
                let decompress_result = decompress_block(
                    &compression_result.data,
                    CompressionAlgorithm::Zstd,
                    wrong_checksum
                );
                prop_assert!(decompress_result.is_err());
            }
        }
    }
}
