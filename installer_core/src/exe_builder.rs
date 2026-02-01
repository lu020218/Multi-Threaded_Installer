//! Executable builder module for creating self-contained installer executables.
//!
//! This module provides functionality to embed package data into an installer
//! executable, creating a single self-contained installer that users can run directly.
//!
//! # Architecture
//!
//! The self-contained installer format:
//! ```text
//! ┌─────────────────────────────────────┐
//! │  Original Installer Executable      │
//! │  (installer_gui.exe or template)    │
//! ├─────────────────────────────────────┤
//! │  Embedded Package Data              │
//! │  (Header + TOC + Metadata + Data)   │
//! ├─────────────────────────────────────┤
//! │  Overlay Marker (16 bytes)          │
//! │  - Magic: "MTIO" (4 bytes)          │
//! │  - Package offset (8 bytes)         │
//! │  - Reserved (4 bytes)               │
//! └─────────────────────────────────────┘
//! ```
//!
//! # Requirements
//! - 5.3: Embed UI resources and package data into installer executable
//! - The packager generates a single .exe that can be run directly

use installer_shared::{InstallerError, Result};
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Read, Seek, SeekFrom, Write};
use std::path::Path;
use tracing::info;

/// Magic bytes for the overlay marker
pub const OVERLAY_MAGIC: &[u8; 4] = b"MTIO";

/// Size of the overlay marker in bytes
pub const OVERLAY_MARKER_SIZE: usize = 16;

/// Overlay marker structure at the end of the executable
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct OverlayMarker {
    /// Magic bytes: "MTIO"
    pub magic: [u8; 4],
    /// Offset where the package data starts
    pub package_offset: u64,
    /// Reserved for future use
    pub reserved: u32,
}

impl OverlayMarker {
    /// Create a new overlay marker
    pub fn new(package_offset: u64) -> Self {
        Self {
            magic: *OVERLAY_MAGIC,
            package_offset,
            reserved: 0,
        }
    }

    /// Serialize the marker to bytes
    pub fn to_bytes(&self) -> [u8; OVERLAY_MARKER_SIZE] {
        let mut bytes = [0u8; OVERLAY_MARKER_SIZE];
        bytes[0..4].copy_from_slice(&self.magic);
        bytes[4..12].copy_from_slice(&self.package_offset.to_le_bytes());
        bytes[12..16].copy_from_slice(&self.reserved.to_le_bytes());
        bytes
    }

    /// Deserialize from bytes
    pub fn from_bytes(bytes: &[u8; OVERLAY_MARKER_SIZE]) -> Option<Self> {
        if &bytes[0..4] != OVERLAY_MAGIC {
            return None;
        }
        
        let package_offset = u64::from_le_bytes(bytes[4..12].try_into().ok()?);
        let reserved = u32::from_le_bytes(bytes[12..16].try_into().ok()?);
        
        Some(Self {
            magic: *OVERLAY_MAGIC,
            package_offset,
            reserved,
        })
    }
}

/// Build a self-contained installer executable.
///
/// This function takes an installer template executable and a package data file,
/// and combines them into a single self-contained installer.
///
/// # Arguments
/// * `template_exe` - Path to the installer template executable (installer_gui.exe)
/// * `package_data` - Path to the package data file (.pkg)
/// * `output_exe` - Path for the output self-contained installer
///
/// # Returns
/// * `Ok(u64)` - Size of the created installer in bytes
/// * `Err(InstallerError)` - On failure
pub fn build_self_contained_installer(
    template_exe: &Path,
    package_data: &Path,
    output_exe: &Path,
) -> Result<u64> {
    info!(
        "Building self-contained installer: {:?} + {:?} -> {:?}",
        template_exe, package_data, output_exe
    );

    // Read the template executable
    let template_data = fs::read(template_exe).map_err(|e| {
        InstallerError::Io(std::io::Error::new(
            e.kind(),
            format!("Failed to read template executable {:?}: {}", template_exe, e),
        ))
    })?;

    let template_size = template_data.len() as u64;
    info!("Template executable size: {} bytes", template_size);

    // Read the package data
    let package_data_bytes = fs::read(package_data).map_err(|e| {
        InstallerError::Io(std::io::Error::new(
            e.kind(),
            format!("Failed to read package data {:?}: {}", package_data, e),
        ))
    })?;

    info!("Package data size: {} bytes", package_data_bytes.len());

    // Create the output file
    let output_file = File::create(output_exe)?;
    let mut writer = BufWriter::new(output_file);

    // Write the template executable
    writer.write_all(&template_data)?;

    // The package data starts right after the template
    let package_offset = template_size;

    // Write the package data
    writer.write_all(&package_data_bytes)?;

    // Write the overlay marker at the end
    let marker = OverlayMarker::new(package_offset);
    writer.write_all(&marker.to_bytes())?;

    writer.flush()?;

    let total_size = template_size + package_data_bytes.len() as u64 + OVERLAY_MARKER_SIZE as u64;
    info!(
        "Created self-contained installer: {} bytes (template: {}, package: {}, marker: {})",
        total_size,
        template_size,
        package_data_bytes.len(),
        OVERLAY_MARKER_SIZE
    );

    Ok(total_size)
}

/// Build a self-contained installer from in-memory package data.
///
/// This is useful when the packager wants to directly embed the package
/// without writing to an intermediate file.
///
/// # Arguments
/// * `template_exe` - Path to the installer template executable
/// * `package_data` - Package data bytes
/// * `output_exe` - Path for the output self-contained installer
///
/// # Returns
/// * `Ok(u64)` - Size of the created installer in bytes
pub fn build_self_contained_installer_from_memory(
    template_exe: &Path,
    package_data: &[u8],
    output_exe: &Path,
) -> Result<u64> {
    info!(
        "Building self-contained installer from memory: {:?} -> {:?}",
        template_exe, output_exe
    );

    // Read the template executable
    let template_data = fs::read(template_exe).map_err(|e| {
        InstallerError::Io(std::io::Error::new(
            e.kind(),
            format!("Failed to read template executable {:?}: {}", template_exe, e),
        ))
    })?;

    let template_size = template_data.len() as u64;

    // Create the output file
    let output_file = File::create(output_exe)?;
    let mut writer = BufWriter::new(output_file);

    // Write the template executable
    writer.write_all(&template_data)?;

    // The package data starts right after the template
    let package_offset = template_size;

    // Write the package data
    writer.write_all(package_data)?;

    // Write the overlay marker at the end
    let marker = OverlayMarker::new(package_offset);
    writer.write_all(&marker.to_bytes())?;

    writer.flush()?;

    let total_size = template_size + package_data.len() as u64 + OVERLAY_MARKER_SIZE as u64;
    info!("Created self-contained installer: {} bytes", total_size);

    Ok(total_size)
}

/// Check if an executable has embedded package data.
///
/// # Arguments
/// * `exe_path` - Path to the executable to check
///
/// # Returns
/// * `Ok(Some(OverlayMarker))` - If the executable has embedded data
/// * `Ok(None)` - If no embedded data found
/// * `Err(InstallerError)` - On read failure
pub fn check_embedded_package(exe_path: &Path) -> Result<Option<OverlayMarker>> {
    let file = File::open(exe_path)?;
    let file_size = file.metadata()?.len();

    if file_size < OVERLAY_MARKER_SIZE as u64 {
        return Ok(None);
    }

    let mut reader = BufReader::new(file);

    // Seek to the last 16 bytes
    reader.seek(SeekFrom::End(-(OVERLAY_MARKER_SIZE as i64)))?;

    let mut marker_bytes = [0u8; OVERLAY_MARKER_SIZE];
    reader.read_exact(&mut marker_bytes)?;

    Ok(OverlayMarker::from_bytes(&marker_bytes))
}

/// Read embedded package data from an executable.
///
/// # Arguments
/// * `exe_path` - Path to the executable
///
/// # Returns
/// * `Ok(Vec<u8>)` - The embedded package data
/// * `Err(InstallerError)` - If no embedded data or read failure
pub fn read_embedded_package(exe_path: &Path) -> Result<Vec<u8>> {
    let marker = check_embedded_package(exe_path)?.ok_or_else(|| {
        InstallerError::InvalidFormat("No embedded package data found in executable".to_string())
    })?;

    let file = File::open(exe_path)?;
    let file_size = file.metadata()?.len();
    let mut reader = BufReader::new(file);

    // Calculate the size of the package data
    // Total size = template + package + marker
    // Package size = file_size - package_offset - marker_size
    let package_size = file_size - marker.package_offset - OVERLAY_MARKER_SIZE as u64;

    // Seek to the package data
    reader.seek(SeekFrom::Start(marker.package_offset))?;

    // Read the package data
    let mut package_data = vec![0u8; package_size as usize];
    reader.read_exact(&mut package_data)?;

    info!(
        "Read embedded package: {} bytes from offset {}",
        package_size, marker.package_offset
    );

    Ok(package_data)
}

/// Extract embedded package to a temporary file.
///
/// This is useful when the installer needs to work with the package
/// as a file rather than in memory.
///
/// # Arguments
/// * `exe_path` - Path to the executable
/// * `output_path` - Path to write the extracted package
///
/// # Returns
/// * `Ok(u64)` - Size of the extracted package
pub fn extract_embedded_package(exe_path: &Path, output_path: &Path) -> Result<u64> {
    let package_data = read_embedded_package(exe_path)?;
    let size = package_data.len() as u64;

    fs::write(output_path, &package_data)?;

    info!("Extracted embedded package to {:?}: {} bytes", output_path, size);

    Ok(size)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_overlay_marker_roundtrip() {
        let marker = OverlayMarker::new(12345678);
        let bytes = marker.to_bytes();
        let restored = OverlayMarker::from_bytes(&bytes).unwrap();

        assert_eq!(restored.magic, *OVERLAY_MAGIC);
        assert_eq!(restored.package_offset, 12345678);
    }

    #[test]
    fn test_overlay_marker_invalid_magic() {
        let mut bytes = [0u8; OVERLAY_MARKER_SIZE];
        bytes[0..4].copy_from_slice(b"XXXX");

        assert!(OverlayMarker::from_bytes(&bytes).is_none());
    }

    #[test]
    fn test_build_and_read_embedded_package() {
        let dir = tempdir().unwrap();

        // Create a fake template executable
        let template_path = dir.path().join("template.exe");
        let template_data = b"FAKE_EXE_HEADER_DATA_HERE";
        fs::write(&template_path, template_data).unwrap();

        // Create fake package data
        let package_data = b"PACKAGE_DATA_CONTENT_HERE_WITH_MORE_STUFF";

        // Build self-contained installer
        let output_path = dir.path().join("installer.exe");
        let size = build_self_contained_installer_from_memory(
            &template_path,
            package_data,
            &output_path,
        )
        .unwrap();

        assert_eq!(
            size,
            template_data.len() as u64 + package_data.len() as u64 + OVERLAY_MARKER_SIZE as u64
        );

        // Check embedded package
        let marker = check_embedded_package(&output_path).unwrap().unwrap();
        assert_eq!(marker.package_offset, template_data.len() as u64);

        // Read embedded package
        let read_data = read_embedded_package(&output_path).unwrap();
        assert_eq!(read_data, package_data);
    }

    #[test]
    fn test_no_embedded_package() {
        let dir = tempdir().unwrap();

        // Create a regular file without embedded data
        let file_path = dir.path().join("regular.exe");
        fs::write(&file_path, b"JUST_A_REGULAR_FILE").unwrap();

        // Should return None
        let result = check_embedded_package(&file_path).unwrap();
        assert!(result.is_none());
    }
}
