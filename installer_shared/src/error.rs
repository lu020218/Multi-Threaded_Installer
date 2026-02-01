//! Error types and Result alias for the installer system.
//!
//! This module defines the [`InstallerError`] enum which covers all possible
//! error conditions that can occur during packaging and installation.
//!
//! # Error Categories
//!
//! - **IO Errors**: File read/write failures, permission issues
//! - **Format Errors**: Invalid package format, magic number mismatch
//! - **Checksum Errors**: Data corruption, integrity verification failures
//! - **Resource Errors**: Insufficient disk space, memory issues
//! - **Platform Errors**: Registry access failures, process operations
//! - **Configuration Errors**: JSON parsing failures, validation errors
//!
//! # Example
//!
//! ```rust
//! use installer_shared::{InstallerError, Result};
//!
//! fn validate_package(magic: &[u8]) -> Result<()> {
//!     if magic != b"MTI2" {
//!         return Err(InstallerError::InvalidFormat(
//!             format!("Invalid magic: {:?}", magic)
//!         ));
//!     }
//!     Ok(())
//! }
//! ```

use thiserror::Error;

/// Main error type for the installer system.
///
/// This enum covers all possible error conditions that can occur during
/// packaging and installation operations. Each variant includes relevant
/// context information for debugging.
#[derive(Error, Debug)]
pub enum InstallerError {
    /// IO error during file operations.
    ///
    /// Wraps standard library IO errors for file read/write operations.
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),

    /// Invalid package format detected.
    ///
    /// Returned when the package structure doesn't match expected format,
    /// such as invalid magic numbers or unsupported versions.
    #[error("Invalid package format: {0}")]
    InvalidFormat(String),

    /// Checksum verification failed.
    ///
    /// Indicates data corruption - the calculated checksum doesn't match
    /// the expected value stored in the package.
    #[error("Checksum mismatch: expected {expected:08x}, got {actual:08x}")]
    ChecksumMismatch {
        /// Expected CRC32 checksum value
        expected: u32,
        /// Actual calculated CRC32 checksum
        actual: u32,
    },

    /// Insufficient disk space for installation.
    ///
    /// The target drive doesn't have enough free space to complete
    /// the installation (including the 100MB safety buffer).
    #[error("Insufficient disk space: required {required} bytes, available {available} bytes")]
    InsufficientDiskSpace {
        /// Required space in bytes (including buffer)
        required: u64,
        /// Available space in bytes
        available: u64,
    },

    /// Unsupported compression algorithm.
    ///
    /// The package uses a compression algorithm that isn't supported
    /// by this version of the installer.
    #[error("Unsupported compression algorithm: {0}")]
    UnsupportedAlgorithm(String),

    /// Platform-specific operation failed.
    ///
    /// Covers Windows-specific operations like registry access,
    /// shortcut creation, and privilege elevation.
    #[error("Platform error: {0}")]
    Platform(String),

    /// Configuration parsing or validation error.
    ///
    /// Returned when packager.json is malformed or contains invalid values.
    #[error("Configuration error: {0}")]
    Config(String),

    /// Target process is still running.
    ///
    /// Installation cannot proceed because the application being
    /// installed/updated is currently running.
    #[error("Process is running: {0}")]
    ProcessRunning(String),

    /// Permission denied for operation.
    ///
    /// The operation requires elevated privileges that weren't granted.
    #[error("Permission denied: {0}")]
    PermissionDenied(String),

    /// Windows version check failed.
    ///
    /// The current Windows version doesn't meet the minimum requirements
    /// specified in the package metadata.
    #[error("Version check failed: {0}")]
    VersionCheckFailed(String),

    /// Serialization/deserialization error.
    ///
    /// Failed to serialize or deserialize data (MessagePack, JSON).
    #[error("Serialization error: {0}")]
    Serialization(String),

    /// Decompression error.
    ///
    /// Failed to decompress data block (Zstd or LZMA).
    #[error("Decompression error: {0}")]
    Decompression(String),

    /// UI resources error.
    ///
    /// Failed to load, validate, or extract UI resources.
    #[error("UI resources error: {0}")]
    UiResources(String),

    /// Rollback operation failed.
    ///
    /// Failed to clean up after an installation failure.
    #[error("Rollback error: {0}")]
    Rollback(String),
}

/// Result type alias using InstallerError.
///
/// This is the standard Result type used throughout the installer system.
/// All fallible operations return this type.
///
/// # Example
///
/// ```rust
/// use installer_shared::Result;
///
/// fn read_config() -> Result<String> {
///     // ... implementation
///     Ok("config".to_string())
/// }
/// ```
pub type Result<T> = std::result::Result<T, InstallerError>;
