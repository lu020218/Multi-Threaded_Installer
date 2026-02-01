//! Package format data structures.
//!
//! Defines the binary format for installer packages including:
//! - PackageHeader
//! - TOC (Table of Contents)
//! - Metadata
//! - Footer

use serde::{Deserialize, Serialize};

/// Magic number for package header: "MTI2"
pub const HEADER_MAGIC: [u8; 4] = *b"MTI2";

/// Magic number for package footer: "MTIF"
pub const FOOTER_MAGIC: [u8; 4] = *b"MTIF";

/// Current package format version.
pub const FORMAT_VERSION: u32 = 1;

/// Package header structure.
/// Contains magic number, version, and offsets to other sections.
/// Fields are ordered to avoid padding in the C representation.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct PackageHeader {
    /// Offset to TOC section
    pub toc_offset: u64,
    /// Size of TOC section
    pub toc_size: u64,
    /// Offset to metadata section
    pub metadata_offset: u64,
    /// Size of metadata section
    pub metadata_size: u64,
    /// Offset to data section
    pub data_offset: u64,
    /// Size of data section
    pub data_size: u64,
    /// Offset to UI resources section (0 if not present)
    pub ui_resources_offset: u64,
    /// Size of UI resources section (0 if not present)
    pub ui_resources_size: u64,
    /// Magic number: b"MTI2"
    pub magic: [u8; 4],
    /// Format version number
    pub version: u32,
    /// Size of this header
    pub header_size: u32,
    /// Flags (bit 0: UI resources, bit 1: signature, bit 2: compressed metadata)
    pub flags: u32,
    /// Reserved for future use
    pub reserved: [u8; 8],
}

impl Default for PackageHeader {
    fn default() -> Self {
        Self {
            magic: HEADER_MAGIC,
            version: FORMAT_VERSION,
            header_size: std::mem::size_of::<PackageHeader>() as u32,
            toc_offset: 0,
            toc_size: 0,
            metadata_offset: 0,
            metadata_size: 0,
            data_offset: 0,
            data_size: 0,
            ui_resources_offset: 0,
            ui_resources_size: 0,
            flags: 0,
            reserved: [0; 8],
        }
    }
}

impl PackageHeader {
    /// Check if the package contains UI resources.
    pub fn has_ui_resources(&self) -> bool {
        (self.flags & flags::HAS_UI_RESOURCES) != 0
    }

    /// Set the UI resources flag.
    pub fn set_ui_resources_flag(&mut self, has_resources: bool) {
        if has_resources {
            self.flags |= flags::HAS_UI_RESOURCES;
        } else {
            self.flags &= !flags::HAS_UI_RESOURCES;
        }
    }
}

/// Header flags.
pub mod flags {
    /// Package contains embedded UI resources.
    pub const HAS_UI_RESOURCES: u32 = 1 << 0;
    /// Package has a signature block.
    pub const HAS_SIGNATURE: u32 = 1 << 1;
    /// Metadata is compressed.
    pub const COMPRESSED_METADATA: u32 = 1 << 2;
}

/// TOC header containing counts.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct TocHeader {
    /// Number of file entries
    pub file_count: u32,
    /// Number of block entries
    pub block_count: u32,
    /// TOC format version
    pub toc_version: u32,
    /// Reserved
    pub reserved: u32,
}

/// File entry in the TOC.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FileEntry {
    /// Relative file path
    pub path: String,
    /// Original (uncompressed) size
    pub original_size: u64,
    /// File permissions/mode
    pub mode: u32,
    /// Index of the first block containing this file's data
    pub first_block_index: u32,
    /// Number of blocks this file spans
    pub block_count: u32,
    /// CRC32 checksum of original file content
    pub checksum: u32,
}

/// Compression algorithm identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum CompressionAlgorithm {
    /// Zstandard compression
    Zstd = 0,
    /// LZMA compression
    Lzma = 1,
}

impl Default for CompressionAlgorithm {
    fn default() -> Self {
        Self::Zstd
    }
}

/// Block entry in the TOC.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct BlockEntry {
    /// Offset within the data section
    pub offset: u64,
    /// Compressed size
    pub compressed_size: u64,
    /// Original (uncompressed) size
    pub original_size: u64,
    /// CRC32 checksum of compressed data
    pub checksum: u32,
    /// Compression algorithm used
    pub algorithm: CompressionAlgorithm,
}

/// Package metadata (serialized with MessagePack).
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PackageMetadata {
    /// Application name
    pub app_name: String,
    /// Application version
    pub version: String,
    /// Default installation directory
    pub default_install_dir: String,
    /// Vendor/publisher name
    pub vendor: Option<String>,
    /// License text
    pub license_text: Option<String>,
    /// Whether admin privileges are required
    pub require_admin: bool,
    /// Path to icon file
    pub icon_path: Option<String>,
    /// UI theme name
    pub ui_theme: Option<String>,
    /// Minimum Windows version requirement
    pub min_windows_version: Option<WindowsVersion>,
    /// Custom registry entries
    pub registry_entries: Vec<RegistryEntry>,
    /// Enable auto-startup
    pub auto_startup: bool,
    /// Create desktop icons
    pub desktop_icons: bool,
    /// Process name to check before installation
    pub process_name: Option<String>,
    /// CRC32 checksum of embedded UI resources
    pub ui_resources_checksum: Option<u32>,
}

impl Default for PackageMetadata {
    fn default() -> Self {
        Self {
            app_name: String::from("MyApp"),
            version: String::from("1.0.0"),
            default_install_dir: String::from("%ProgramFiles%"),
            vendor: None,
            license_text: None,
            require_admin: false,
            icon_path: None,
            ui_theme: None,
            min_windows_version: None,
            registry_entries: Vec::new(),
            auto_startup: false,
            desktop_icons: false,
            process_name: None,
            ui_resources_checksum: None,
        }
    }
}

/// Windows version specification.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct WindowsVersion {
    /// Major version number
    pub major: u16,
    /// Minor version number
    pub minor: u16,
    /// Build number
    pub build: u32,
}

/// Registry entry for custom registry operations.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RegistryEntry {
    /// Registry path (e.g., "HKEY_CURRENT_USER\\Software\\MyApp")
    pub path: String,
    /// Key name
    pub key: String,
    /// Value
    pub value: String,
    /// Value type
    pub value_type: RegistryValueType,
}

/// Registry value types.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum RegistryValueType {
    /// REG_SZ
    String,
    /// REG_DWORD
    Dword,
    /// REG_EXPAND_SZ
    ExpandString,
}

/// Package footer for quick location of sections.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct PackageFooter {
    /// Magic number: b"MTIF"
    pub footer_magic: [u8; 4],
    /// Offset to header
    pub header_offset: u64,
    /// Offset to TOC
    pub toc_offset: u64,
    /// Offset to metadata
    pub metadata_offset: u64,
    /// Offset to data
    pub data_offset: u64,
    /// Offset to UI resources (0 if not present)
    pub ui_resources_offset: u64,
    /// CRC32 of entire package
    pub crc32: u32,
    /// Reserved
    pub reserved: [u8; 4],
}

impl Default for PackageFooter {
    fn default() -> Self {
        Self {
            footer_magic: FOOTER_MAGIC,
            header_offset: 0,
            toc_offset: 0,
            metadata_offset: 0,
            data_offset: 0,
            ui_resources_offset: 0,
            crc32: 0,
            reserved: [0; 4],
        }
    }
}
