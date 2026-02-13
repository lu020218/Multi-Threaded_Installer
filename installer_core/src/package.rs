//! Package format reading and writing.
//!
//! Handles serialization and deserialization of the installer package format.
//!
//! # Package Format Structure
//!
//! The package format consists of five sections in order:
//! 1. Header - Contains magic number, version, and offsets
//! 2. TOC (Table of Contents) - File and block entries with CRC32 checksum
//! 3. Metadata - Application metadata in MessagePack format
//! 4. Data Blocks - Compressed file data
//! 5. Footer - Quick location offsets and package CRC32
//!
//! All numeric values use little-endian byte order.

use crc32fast::Hasher;
use installer_shared::{
    BlockEntry, FileEntry, InstallerError, PackageFooter, PackageHeader, PackageMetadata, Result,
    TocHeader, FOOTER_MAGIC, FORMAT_VERSION, HEADER_MAGIC,
};
use std::io::{Read, Seek, SeekFrom, Write};

/// Calculate CRC32 checksum for a byte slice.
pub fn calculate_crc32(data: &[u8]) -> u32 {
    let mut hasher = Hasher::new();
    hasher.update(data);
    hasher.finalize()
}

/// Write package header to a writer.
///
/// Writes all header fields in little-endian byte order.
/// The header contains the magic number "MTI2", format version,
/// and offsets to all other sections.
///
/// # Arguments
/// * `writer` - The writer to write the header to
/// * `header` - The header structure to write
///
/// # Returns
/// * `Ok(())` on success
/// * `Err(InstallerError)` on write failure
pub fn write_header<W: Write>(writer: &mut W, header: &PackageHeader) -> Result<()> {
    // Write all fields in the order they appear in the struct (to match repr(C) layout)
    writer.write_all(&header.toc_offset.to_le_bytes())?;
    writer.write_all(&header.toc_size.to_le_bytes())?;
    writer.write_all(&header.metadata_offset.to_le_bytes())?;
    writer.write_all(&header.metadata_size.to_le_bytes())?;
    writer.write_all(&header.data_offset.to_le_bytes())?;
    writer.write_all(&header.data_size.to_le_bytes())?;
    writer.write_all(&header.ui_resources_offset.to_le_bytes())?;
    writer.write_all(&header.ui_resources_size.to_le_bytes())?;
    writer.write_all(&header.magic)?;
    writer.write_all(&header.version.to_le_bytes())?;
    writer.write_all(&header.header_size.to_le_bytes())?;
    writer.write_all(&header.flags.to_le_bytes())?;
    writer.write_all(&header.reserved)?;
    Ok(())
}

/// Read package header from a reader.
///
/// Reads and validates the header, checking the magic number and version.
/// All numeric fields are read in little-endian byte order.
///
/// # Arguments
/// * `reader` - The reader to read the header from
///
/// # Returns
/// * `Ok(PackageHeader)` on success
/// * `Err(InstallerError::InvalidFormat)` if magic number or version is invalid
/// * `Err(InstallerError::Io)` on read failure
pub fn read_header<R: Read>(reader: &mut R) -> Result<PackageHeader> {
    let mut buf4 = [0u8; 4];
    let mut buf8 = [0u8; 8];

    // Read fields in the order they appear in the struct (to match repr(C) layout)
    reader.read_exact(&mut buf8)?;
    let toc_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let toc_size = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let metadata_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let metadata_size = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let data_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let data_size = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let ui_resources_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let ui_resources_size = u64::from_le_bytes(buf8);

    let mut magic = [0u8; 4];
    reader.read_exact(&mut magic)?;

    if magic != HEADER_MAGIC {
        return Err(InstallerError::InvalidFormat(format!(
            "Invalid header magic: expected {:?}, got {:?}",
            HEADER_MAGIC, magic
        )));
    }

    reader.read_exact(&mut buf4)?;
    let version = u32::from_le_bytes(buf4);

    // Validate version
    if version != FORMAT_VERSION {
        return Err(InstallerError::InvalidFormat(format!(
            "Unsupported package version: expected {}, got {}",
            FORMAT_VERSION, version
        )));
    }

    reader.read_exact(&mut buf4)?;
    let header_size = u32::from_le_bytes(buf4);

    reader.read_exact(&mut buf4)?;
    let flags = u32::from_le_bytes(buf4);

    let mut reserved = [0u8; 8];
    reader.read_exact(&mut reserved)?;

    Ok(PackageHeader {
        magic,
        version,
        header_size,
        toc_offset,
        toc_size,
        metadata_offset,
        metadata_size,
        data_offset,
        data_size,
        ui_resources_offset,
        ui_resources_size,
        flags,
        reserved,
    })
}

/// TOC data structure for serialization.
///
/// Contains the TOC header, file entries, and block entries.
/// The TOC is serialized using MessagePack format.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize, PartialEq)]
pub struct Toc {
    /// TOC header with counts
    pub header: TocHeader,
    /// File entries
    pub files: Vec<FileEntry>,
    /// Block entries
    pub blocks: Vec<BlockEntry>,
}

/// TOC with checksum for serialization.
///
/// Wraps the TOC data with a CRC32 checksum for integrity verification.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
struct TocWithChecksum {
    /// CRC32 checksum of the serialized TOC data
    checksum: u32,
    /// The actual TOC data
    toc: Toc,
}

/// Write TOC to a writer using MessagePack with CRC32 checksum.
///
/// The TOC is serialized with a CRC32 checksum prepended for integrity verification.
///
/// # Arguments
/// * `writer` - The writer to write the TOC to
/// * `toc` - The TOC structure to write
///
/// # Returns
/// * `Ok(usize)` - The number of bytes written
/// * `Err(InstallerError::Serialization)` on serialization failure
/// * `Err(InstallerError::Io)` on write failure
pub fn write_toc<W: Write>(writer: &mut W, toc: &Toc) -> Result<usize> {
    // First serialize the TOC to calculate checksum
    let toc_data =
        rmp_serde::to_vec(toc).map_err(|e| InstallerError::Serialization(e.to_string()))?;

    // Calculate CRC32 checksum
    let checksum = calculate_crc32(&toc_data);

    // Write checksum first (4 bytes, little-endian)
    writer.write_all(&checksum.to_le_bytes())?;

    // Write TOC data
    writer.write_all(&toc_data)?;

    Ok(4 + toc_data.len())
}

/// Read TOC from a reader with CRC32 checksum verification.
///
/// Reads the TOC and verifies its CRC32 checksum.
///
/// # Arguments
/// * `reader` - The reader to read the TOC from
/// * `size` - The total size of the TOC section (including checksum)
///
/// # Returns
/// * `Ok(Toc)` on success
/// * `Err(InstallerError::ChecksumMismatch)` if checksum verification fails
/// * `Err(InstallerError::Serialization)` on deserialization failure
/// * `Err(InstallerError::Io)` on read failure
pub fn read_toc<R: Read>(reader: &mut R, size: usize) -> Result<Toc> {
    if size < 4 {
        return Err(InstallerError::InvalidFormat(
            "TOC size too small to contain checksum".to_string(),
        ));
    }

    // Read checksum (4 bytes, little-endian)
    let mut checksum_bytes = [0u8; 4];
    reader.read_exact(&mut checksum_bytes)?;
    let expected_checksum = u32::from_le_bytes(checksum_bytes);

    // Read TOC data
    let toc_data_size = size - 4;
    let mut toc_data = vec![0u8; toc_data_size];
    reader.read_exact(&mut toc_data)?;

    // Verify checksum
    let actual_checksum = calculate_crc32(&toc_data);
    if actual_checksum != expected_checksum {
        return Err(InstallerError::ChecksumMismatch {
            expected: expected_checksum,
            actual: actual_checksum,
        });
    }

    // Deserialize TOC
    rmp_serde::from_slice(&toc_data).map_err(|e| InstallerError::Serialization(e.to_string()))
}

/// Serialize TOC to bytes (without checksum wrapper).
///
/// This is useful for calculating the TOC size before writing.
pub fn serialize_toc(toc: &Toc) -> Result<Vec<u8>> {
    rmp_serde::to_vec(toc).map_err(|e| InstallerError::Serialization(e.to_string()))
}

/// Calculate the size of a serialized TOC including checksum.
pub fn calculate_toc_size(toc: &Toc) -> Result<usize> {
    let toc_data = serialize_toc(toc)?;
    Ok(4 + toc_data.len()) // 4 bytes for checksum + TOC data
}

/// Write metadata to a writer using MessagePack.
///
/// Serializes the package metadata using MessagePack format.
///
/// # Arguments
/// * `writer` - The writer to write the metadata to
/// * `metadata` - The metadata structure to write
///
/// # Returns
/// * `Ok(usize)` - The number of bytes written
/// * `Err(InstallerError::Serialization)` on serialization failure
/// * `Err(InstallerError::Io)` on write failure
pub fn write_metadata<W: Write>(writer: &mut W, metadata: &PackageMetadata) -> Result<usize> {
    let data =
        rmp_serde::to_vec(metadata).map_err(|e| InstallerError::Serialization(e.to_string()))?;
    writer.write_all(&data)?;
    Ok(data.len())
}

/// Read metadata from a reader.
///
/// Deserializes the package metadata from MessagePack format.
///
/// # Arguments
/// * `reader` - The reader to read the metadata from
/// * `size` - The size of the metadata section
///
/// # Returns
/// * `Ok(PackageMetadata)` on success
/// * `Err(InstallerError::Serialization)` on deserialization failure
/// * `Err(InstallerError::Io)` on read failure
pub fn read_metadata<R: Read>(reader: &mut R, size: usize) -> Result<PackageMetadata> {
    let mut data = vec![0u8; size];
    reader.read_exact(&mut data)?;

    rmp_serde::from_slice(&data).map_err(|e| InstallerError::Serialization(e.to_string()))
}

/// Serialize metadata to bytes.
///
/// This is useful for calculating the metadata size before writing.
pub fn serialize_metadata(metadata: &PackageMetadata) -> Result<Vec<u8>> {
    rmp_serde::to_vec(metadata).map_err(|e| InstallerError::Serialization(e.to_string()))
}

/// Write package footer to a writer.
///
/// Writes the footer with magic number "MTIF" and all section offsets.
/// All numeric fields are written in little-endian byte order.
///
/// # Arguments
/// * `writer` - The writer to write the footer to
/// * `footer` - The footer structure to write
///
/// # Returns
/// * `Ok(())` on success
/// * `Err(InstallerError::Io)` on write failure
pub fn write_footer<W: Write>(writer: &mut W, footer: &PackageFooter) -> Result<()> {
    writer.write_all(&footer.footer_magic)?;
    writer.write_all(&footer.header_offset.to_le_bytes())?;
    writer.write_all(&footer.toc_offset.to_le_bytes())?;
    writer.write_all(&footer.metadata_offset.to_le_bytes())?;
    writer.write_all(&footer.data_offset.to_le_bytes())?;
    writer.write_all(&footer.ui_resources_offset.to_le_bytes())?;
    writer.write_all(&footer.crc32.to_le_bytes())?;
    writer.write_all(&footer.reserved)?;
    Ok(())
}

/// Read package footer from a reader.
///
/// Seeks to the end of the file and reads the footer for quick section location.
/// Validates the footer magic number "MTIF".
///
/// # Arguments
/// * `reader` - The reader to read the footer from (must support seeking)
///
/// # Returns
/// * `Ok(PackageFooter)` on success
/// * `Err(InstallerError::InvalidFormat)` if magic number is invalid
/// * `Err(InstallerError::Io)` on read or seek failure
pub fn read_footer<R: Read + Seek>(reader: &mut R) -> Result<PackageFooter> {
    // Seek to footer position (end - footer size)
    let footer_size = footer_size() as i64;
    reader.seek(SeekFrom::End(-footer_size))?;

    let mut magic = [0u8; 4];
    reader.read_exact(&mut magic)?;

    if magic != FOOTER_MAGIC {
        return Err(InstallerError::InvalidFormat(format!(
            "Invalid footer magic: expected {:?}, got {:?}",
            FOOTER_MAGIC, magic
        )));
    }

    let mut buf8 = [0u8; 8];
    let mut buf4 = [0u8; 4];

    reader.read_exact(&mut buf8)?;
    let header_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let toc_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let metadata_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let data_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf8)?;
    let ui_resources_offset = u64::from_le_bytes(buf8);

    reader.read_exact(&mut buf4)?;
    let crc32 = u32::from_le_bytes(buf4);

    let mut reserved = [0u8; 4];
    reader.read_exact(&mut reserved)?;

    Ok(PackageFooter {
        footer_magic: magic,
        header_offset,
        toc_offset,
        metadata_offset,
        data_offset,
        ui_resources_offset,
        crc32,
        reserved,
    })
}

/// Get the size of the package footer in bytes.
pub const fn footer_size() -> usize {
    // Serialized footer layout is packed (no padding):
    // magic(4) + 5*u64(40) + crc32(4) + reserved(4) = 52 bytes
    52
}

/// Get the size of the package header in bytes.
pub const fn header_size() -> usize {
    std::mem::size_of::<PackageHeader>()
}

#[cfg(test)]
mod tests {
    use super::*;
    use installer_shared::{
        CompressionAlgorithm, RegistryEntry, RegistryValueType, WindowsVersion,
    };
    use std::io::Cursor;

    #[test]
    fn test_header_roundtrip() {
        let header = PackageHeader {
            magic: HEADER_MAGIC,
            version: FORMAT_VERSION,
            header_size: 88,
            toc_offset: 100,
            toc_size: 200,
            metadata_offset: 300,
            metadata_size: 50,
            data_offset: 350,
            data_size: 1000,
            ui_resources_offset: 1350,
            ui_resources_size: 500,
            flags: 0,
            reserved: [0; 8],
        };

        let mut buffer = Vec::new();
        write_header(&mut buffer, &header).unwrap();

        let mut cursor = Cursor::new(buffer);
        let read_header = read_header(&mut cursor).unwrap();

        assert_eq!(header, read_header);
    }

    #[test]
    fn test_header_invalid_magic() {
        // With reordered fields, magic is at offset 64 (after 8 u64 fields)
        let mut buffer = vec![0u8; 88]; // Full header size
                                        // Write 8 u64 fields (64 bytes)
        for i in 0..8 {
            buffer[i * 8..(i + 1) * 8].copy_from_slice(&0u64.to_le_bytes());
        }
        // Write invalid magic at offset 64
        buffer[64..68].copy_from_slice(b"XXXX");
        // Write valid version at offset 68
        buffer[68..72].copy_from_slice(&FORMAT_VERSION.to_le_bytes());
        // Write header_size at offset 72
        buffer[72..76].copy_from_slice(&88u32.to_le_bytes());
        // Write flags at offset 76
        buffer[76..80].copy_from_slice(&0u32.to_le_bytes());
        // Reserved at offset 80-88 (already zeros)

        let mut cursor = Cursor::new(buffer);
        let result = read_header(&mut cursor);

        assert!(matches!(result, Err(InstallerError::InvalidFormat(_))));
    }

    #[test]
    fn test_header_invalid_version() {
        // With reordered fields, version is at offset 68 (after 8 u64 fields + 4 byte magic)
        let mut buffer = vec![0u8; 88]; // Full header size
                                        // Write 8 u64 fields (64 bytes)
        for i in 0..8 {
            buffer[i * 8..(i + 1) * 8].copy_from_slice(&0u64.to_le_bytes());
        }
        // Write valid magic at offset 64
        buffer[64..68].copy_from_slice(&HEADER_MAGIC);
        // Write invalid version at offset 68
        buffer[68..72].copy_from_slice(&999u32.to_le_bytes());
        // Write header_size at offset 72
        buffer[72..76].copy_from_slice(&88u32.to_le_bytes());
        // Write flags at offset 76
        buffer[76..80].copy_from_slice(&0u32.to_le_bytes());
        // Reserved at offset 80-88 (already zeros)

        let mut cursor = Cursor::new(buffer);
        let result = read_header(&mut cursor);

        assert!(matches!(result, Err(InstallerError::InvalidFormat(_))));
    }

    #[test]
    fn test_toc_roundtrip() {
        let toc = Toc {
            header: TocHeader {
                file_count: 2,
                block_count: 3,
                toc_version: 1,
                reserved: 0,
            },
            files: vec![
                FileEntry {
                    path: "test/file1.txt".to_string(),
                    original_size: 1024,
                    mode: 0o644,
                    first_block_index: 0,
                    block_count: 1,
                    checksum: 0x12345678,
                },
                FileEntry {
                    path: "test/file2.bin".to_string(),
                    original_size: 2048,
                    mode: 0o755,
                    first_block_index: 1,
                    block_count: 2,
                    checksum: 0xABCDEF01,
                },
            ],
            blocks: vec![
                BlockEntry {
                    offset: 0,
                    compressed_size: 512,
                    original_size: 1024,
                    checksum: 0x11111111,
                    algorithm: CompressionAlgorithm::Zstd,
                },
                BlockEntry {
                    offset: 512,
                    compressed_size: 1024,
                    original_size: 2048,
                    checksum: 0x22222222,
                    algorithm: CompressionAlgorithm::Lzma,
                },
                BlockEntry {
                    offset: 1536,
                    compressed_size: 256,
                    original_size: 512,
                    checksum: 0x33333333,
                    algorithm: CompressionAlgorithm::Zstd,
                },
            ],
        };

        let mut buffer = Vec::new();
        let written_size = write_toc(&mut buffer, &toc).unwrap();

        assert_eq!(buffer.len(), written_size);

        let mut cursor = Cursor::new(&buffer);
        let read_toc = read_toc(&mut cursor, buffer.len()).unwrap();

        assert_eq!(toc, read_toc);
    }

    #[test]
    fn test_toc_checksum_verification() {
        let toc = Toc {
            header: TocHeader {
                file_count: 1,
                block_count: 1,
                toc_version: 1,
                reserved: 0,
            },
            files: vec![FileEntry {
                path: "test.txt".to_string(),
                original_size: 100,
                mode: 0o644,
                first_block_index: 0,
                block_count: 1,
                checksum: 0,
            }],
            blocks: vec![BlockEntry {
                offset: 0,
                compressed_size: 50,
                original_size: 100,
                checksum: 0,
                algorithm: CompressionAlgorithm::Zstd,
            }],
        };

        let mut buffer = Vec::new();
        write_toc(&mut buffer, &toc).unwrap();

        // Corrupt the data (not the checksum)
        if buffer.len() > 10 {
            buffer[10] ^= 0xFF;
        }

        let mut cursor = Cursor::new(&buffer);
        let result = read_toc(&mut cursor, buffer.len());

        assert!(matches!(
            result,
            Err(InstallerError::ChecksumMismatch { .. })
        ));
    }

    #[test]
    fn test_metadata_roundtrip() {
        let metadata = PackageMetadata {
            app_name: "TestApp".to_string(),
            version: "1.0.0".to_string(),
            default_install_dir: "C:\\Program Files\\TestApp".to_string(),
            vendor: Some("Test Vendor".to_string()),
            license_text: Some("MIT License".to_string()),
            require_admin: true,
            icon_path: Some("icon.ico".to_string()),
            ui_theme: Some("dark".to_string()),
            min_windows_version: Some(WindowsVersion {
                major: 10,
                minor: 0,
                build: 19041,
            }),
            registry_entries: vec![RegistryEntry {
                path: "HKEY_CURRENT_USER\\Software\\TestApp".to_string(),
                key: "InstallPath".to_string(),
                value: "C:\\Program Files\\TestApp".to_string(),
                value_type: RegistryValueType::String,
            }],
            auto_startup: true,
            desktop_icons: true,
            process_name: Some("testapp.exe".to_string()),
            ui_resources_checksum: Some(0x12345678),
            window: None,
            embedded_flow_yaml: None,
            embedded_scripts: Vec::new(),
            embedded_component_manifest: None,
        };

        let mut buffer = Vec::new();
        let written_size = write_metadata(&mut buffer, &metadata).unwrap();

        assert_eq!(buffer.len(), written_size);

        let mut cursor = Cursor::new(&buffer);
        let read_metadata = read_metadata(&mut cursor, buffer.len()).unwrap();

        assert_eq!(metadata, read_metadata);
    }

    #[test]
    fn test_footer_roundtrip() {
        let footer = PackageFooter {
            footer_magic: FOOTER_MAGIC,
            header_offset: 0,
            toc_offset: 100,
            metadata_offset: 500,
            data_offset: 600,
            ui_resources_offset: 1600,
            crc32: 0xDEADBEEF,
            reserved: [0; 4],
        };

        // Create a buffer with some padding before the footer
        let mut buffer = vec![0u8; 100];
        write_footer(
            &mut Cursor::new(&mut buffer[100 - footer_size()..]),
            &footer,
        )
        .unwrap();

        // Actually write footer at the end
        let mut full_buffer = vec![0u8; 100];
        let footer_start = 100 - footer_size();
        let mut footer_cursor = Cursor::new(&mut full_buffer[footer_start..]);
        write_footer(&mut footer_cursor, &footer).unwrap();

        let mut cursor = Cursor::new(&full_buffer);
        let read_footer = read_footer(&mut cursor).unwrap();

        assert_eq!(footer, read_footer);
    }

    #[test]
    fn test_footer_invalid_magic() {
        let mut buffer = vec![0u8; footer_size()];
        buffer[0..4].copy_from_slice(b"XXXX"); // Invalid magic

        let mut cursor = Cursor::new(&buffer);
        let result = read_footer(&mut cursor);

        assert!(matches!(result, Err(InstallerError::InvalidFormat(_))));
    }

    #[test]
    fn test_crc32_calculation() {
        let data = b"Hello, World!";
        let checksum = calculate_crc32(data);

        // Verify same data produces same checksum
        assert_eq!(checksum, calculate_crc32(data));

        // Verify different data produces different checksum
        let different_data = b"Hello, World?";
        assert_ne!(checksum, calculate_crc32(different_data));
    }

    #[test]
    fn test_little_endian_consistency() {
        // Test that values are correctly written in little-endian
        // With reordered fields: u64 fields first, then u32 fields
        let header = PackageHeader {
            magic: HEADER_MAGIC,
            version: 0x01020304,
            header_size: 0x05060708,
            toc_offset: 0x090A0B0C0D0E0F10,
            ..Default::default()
        };

        let mut buffer = Vec::new();
        write_header(&mut buffer, &header).unwrap();

        // toc_offset is first (8 bytes, little-endian: 10 0F 0E 0D 0C 0B 0A 09)
        assert_eq!(buffer[0], 0x10);
        assert_eq!(buffer[1], 0x0F);
        assert_eq!(buffer[2], 0x0E);
        assert_eq!(buffer[3], 0x0D);
        assert_eq!(buffer[4], 0x0C);
        assert_eq!(buffer[5], 0x0B);
        assert_eq!(buffer[6], 0x0A);
        assert_eq!(buffer[7], 0x09);

        // magic is at offset 64 (after 8 u64 fields)
        assert_eq!(&buffer[64..68], &HEADER_MAGIC);

        // version is at offset 68 (little-endian: 04 03 02 01)
        assert_eq!(buffer[68], 0x04);
        assert_eq!(buffer[69], 0x03);
        assert_eq!(buffer[70], 0x02);
        assert_eq!(buffer[71], 0x01);

        // header_size is at offset 72 (little-endian: 08 07 06 05)
        assert_eq!(buffer[72], 0x08);
        assert_eq!(buffer[73], 0x07);
        assert_eq!(buffer[74], 0x06);
        assert_eq!(buffer[75], 0x05);
    }
}

#[cfg(test)]
mod property_tests {
    use super::*;
    use installer_shared::CompressionAlgorithm;
    use proptest::prelude::*;
    use std::io::Cursor;

    // Strategy for generating arbitrary PackageHeader
    fn arb_header() -> impl Strategy<Value = PackageHeader> {
        (
            any::<u64>(), // toc_offset
            any::<u64>(), // toc_size
            any::<u64>(), // metadata_offset
            any::<u64>(), // metadata_size
            any::<u64>(), // data_offset
            any::<u64>(), // data_size
            any::<u64>(), // ui_resources_offset
            any::<u64>(), // ui_resources_size
            any::<u32>(), // flags
        )
            .prop_map(
                |(
                    toc_offset,
                    toc_size,
                    metadata_offset,
                    metadata_size,
                    data_offset,
                    data_size,
                    ui_resources_offset,
                    ui_resources_size,
                    flags,
                )| {
                    PackageHeader {
                        magic: HEADER_MAGIC,
                        version: FORMAT_VERSION,
                        header_size: header_size() as u32,
                        toc_offset,
                        toc_size,
                        metadata_offset,
                        metadata_size,
                        data_offset,
                        data_size,
                        ui_resources_offset,
                        ui_resources_size,
                        flags,
                        reserved: [0; 8],
                    }
                },
            )
    }

    // Strategy for generating arbitrary FileEntry
    fn arb_file_entry() -> impl Strategy<Value = FileEntry> {
        (
            "[a-zA-Z0-9_/]{1,50}", // path
            any::<u64>(),          // original_size
            any::<u32>(),          // mode
            any::<u32>(),          // first_block_index
            1..100u32,             // block_count (at least 1)
            any::<u32>(),          // checksum
        )
            .prop_map(
                |(path, original_size, mode, first_block_index, block_count, checksum)| FileEntry {
                    path,
                    original_size,
                    mode,
                    first_block_index,
                    block_count,
                    checksum,
                },
            )
    }

    // Strategy for generating arbitrary BlockEntry
    fn arb_block_entry() -> impl Strategy<Value = BlockEntry> {
        (
            any::<u64>(), // offset
            any::<u64>(), // compressed_size
            any::<u64>(), // original_size
            any::<u32>(), // checksum
            prop_oneof![
                Just(CompressionAlgorithm::Zstd),
                Just(CompressionAlgorithm::Lzma)
            ],
        )
            .prop_map(
                |(offset, compressed_size, original_size, checksum, algorithm)| BlockEntry {
                    offset,
                    compressed_size,
                    original_size,
                    checksum,
                    algorithm,
                },
            )
    }

    // Strategy for generating arbitrary Toc
    fn arb_toc() -> impl Strategy<Value = Toc> {
        (
            prop::collection::vec(arb_file_entry(), 0..10),
            prop::collection::vec(arb_block_entry(), 0..20),
        )
            .prop_map(|(files, blocks)| Toc {
                header: TocHeader {
                    file_count: files.len() as u32,
                    block_count: blocks.len() as u32,
                    toc_version: 1,
                    reserved: 0,
                },
                files,
                blocks,
            })
    }

    // Strategy for generating arbitrary PackageMetadata
    fn arb_metadata() -> impl Strategy<Value = PackageMetadata> {
        (
            "[a-zA-Z0-9_]{1,30}",                   // app_name
            "[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}", // version
            "[a-zA-Z0-9_/\\\\]{1,50}",              // default_install_dir
            any::<bool>(),                          // require_admin
            any::<bool>(),                          // auto_startup
            any::<bool>(),                          // desktop_icons
        )
            .prop_map(
                |(
                    app_name,
                    version,
                    default_install_dir,
                    require_admin,
                    auto_startup,
                    desktop_icons,
                )| {
                    PackageMetadata {
                        app_name,
                        version,
                        default_install_dir,
                        vendor: None,
                        license_text: None,
                        require_admin,
                        icon_path: None,
                        ui_theme: None,
                        min_windows_version: None,
                        registry_entries: Vec::new(),
                        auto_startup,
                        desktop_icons,
                        process_name: None,
                        ui_resources_checksum: None,
                        window: None,
                        embedded_flow_yaml: None,
                        embedded_scripts: Vec::new(),
                        embedded_component_manifest: None,
                    }
                },
            )
    }

    // Strategy for generating arbitrary PackageFooter
    fn arb_footer() -> impl Strategy<Value = PackageFooter> {
        (
            any::<u64>(), // header_offset
            any::<u64>(), // toc_offset
            any::<u64>(), // metadata_offset
            any::<u64>(), // data_offset
            any::<u64>(), // ui_resources_offset
            any::<u32>(), // crc32
        )
            .prop_map(
                |(
                    header_offset,
                    toc_offset,
                    metadata_offset,
                    data_offset,
                    ui_resources_offset,
                    crc32,
                )| {
                    PackageFooter {
                        footer_magic: FOOTER_MAGIC,
                        header_offset,
                        toc_offset,
                        metadata_offset,
                        data_offset,
                        ui_resources_offset,
                        crc32,
                        reserved: [0; 4],
                    }
                },
            )
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// Property 1: Package Header Round-Trip Consistency
        /// For any valid PackageHeader, writing and reading should produce an equivalent header.
        /// **Validates: Requirements 1.2, 1.8**
        #[test]
        fn prop_header_roundtrip(header in arb_header()) {
            let mut buffer = Vec::new();
            write_header(&mut buffer, &header).unwrap();

            let mut cursor = Cursor::new(buffer);
            let read_back = read_header(&mut cursor).unwrap();

            prop_assert_eq!(header, read_back);
        }

        /// Property 1: TOC Round-Trip Consistency
        /// For any valid TOC, writing and reading should produce an equivalent TOC.
        /// **Validates: Requirements 1.3, 1.4, 2.6**
        #[test]
        fn prop_toc_roundtrip(toc in arb_toc()) {
            let mut buffer = Vec::new();
            let written_size = write_toc(&mut buffer, &toc).unwrap();

            prop_assert_eq!(buffer.len(), written_size);

            let mut cursor = Cursor::new(&buffer);
            let read_back = read_toc(&mut cursor, buffer.len()).unwrap();

            prop_assert_eq!(toc, read_back);
        }

        /// Property 1: Metadata Round-Trip Consistency
        /// For any valid PackageMetadata, writing and reading should produce equivalent metadata.
        /// **Validates: Requirements 1.5, 1.6, 2.7**
        #[test]
        fn prop_metadata_roundtrip(metadata in arb_metadata()) {
            let mut buffer = Vec::new();
            let written_size = write_metadata(&mut buffer, &metadata).unwrap();

            prop_assert_eq!(buffer.len(), written_size);

            let mut cursor = Cursor::new(&buffer);
            let read_back = read_metadata(&mut cursor, buffer.len()).unwrap();

            prop_assert_eq!(metadata, read_back);
        }

        /// Property 1: Footer Round-Trip Consistency
        /// For any valid PackageFooter, writing and reading should produce an equivalent footer.
        /// **Validates: Requirements 1.7**
        #[test]
        fn prop_footer_roundtrip(footer in arb_footer()) {
            // Create buffer with footer at the end
            let mut buffer = vec![0u8; footer_size()];
            {
                let mut cursor = Cursor::new(&mut buffer[..]);
                write_footer(&mut cursor, &footer).unwrap();
            }

            let mut cursor = Cursor::new(&buffer);
            let read_back = read_footer(&mut cursor).unwrap();

            prop_assert_eq!(footer, read_back);
        }

        /// Property: CRC32 Checksum Determinism
        /// For any data, CRC32 calculation should be deterministic.
        #[test]
        fn prop_crc32_deterministic(data in prop::collection::vec(any::<u8>(), 0..1000)) {
            let checksum1 = calculate_crc32(&data);
            let checksum2 = calculate_crc32(&data);

            prop_assert_eq!(checksum1, checksum2);
        }

        /// Property: TOC Checksum Integrity
        /// Corrupted TOC data should fail checksum verification.
        #[test]
        fn prop_toc_checksum_integrity(toc in arb_toc(), corrupt_pos in 0usize..100) {
            let mut buffer = Vec::new();
            write_toc(&mut buffer, &toc).unwrap();

            // Only corrupt if buffer is large enough and position is in data area (after checksum)
            if buffer.len() > 4 && corrupt_pos < buffer.len() - 4 {
                let pos = 4 + (corrupt_pos % (buffer.len() - 4));
                buffer[pos] ^= 0xFF;

                let mut cursor = Cursor::new(&buffer);
                let result = read_toc(&mut cursor, buffer.len());

                // Should either fail with checksum mismatch or serialization error
                prop_assert!(result.is_err());
            }
        }
    }
}
