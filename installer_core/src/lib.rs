//! # installer_core
//!
//! Core installation and packaging logic for the installer system.
//!
//! This crate provides the main functionality for creating installer packages
//! and installing applications. It is designed to be used by both CLI and GUI
//! frontends.
//!
//! ## Modules
//!
//! - [`compression`] - Zstd and LZMA compression/decompression with CRC32 verification
//! - [`filesystem`] - File scanning, block division, and disk operations
//! - [`package`] - Package format reading and writing
//! - [`platform`] - Platform abstraction trait and Windows implementation
//! - [`packager`] - High-level packager API
//! - [`installer`] - High-level installer API
//! - [`uninstall`] - Uninstallation logic
//! - [`ui_resources`] - UI resource embedding and extraction
//! - [`localization`] - Multi-language support
//! - [`logging`] - Logging system configuration
//!
//! ## Quick Start - Creating a Package
//!
//! ```rust,no_run
//! use installer_core::{Packager, PackagerConfig};
//! use std::path::Path;
//!
//! fn main() -> installer_shared::Result<()> {
//!     let config = PackagerConfig {
//!         application_name: "MyApp".to_string(),
//!         version: "1.0.0".to_string(),
//!         ..Default::default()
//!     };
//!
//!     let packager = Packager::new(config)?;
//!     let stats = packager.build_package(
//!         Path::new("./input"),
//!         Path::new("./output.mti"),
//!         None,
//!         |progress| println!("{:?}", progress),
//!     )?;
//!
//!     println!("Created package with {} files", stats.total_files);
//!     Ok(())
//! }
//! ```
//!
//! ## Quick Start - Installing a Package
//!
//! ```rust,no_run
//! use installer_core::{Installer, InstallOptions};
//! use std::path::PathBuf;
//!
//! fn main() -> installer_shared::Result<()> {
//!     let installer = Installer::new(PathBuf::from("./package.mti"))?;
//!     
//!     let options = InstallOptions {
//!         install_dir: PathBuf::from("C:\\Program Files\\MyApp"),
//!         create_shortcuts: true,
//!         ..Default::default()
//!     };
//!
//!     let stats = installer.install(options, |progress| {
//!         println!("Progress: {:.1}%", progress.percentage());
//!     })?;
//!
//!     println!("Installed {} files", stats.installed_files);
//!     Ok(())
//! }
//! ```
//!
//! ## Architecture
//!
//! The crate is organized in layers:
//!
//! ```text
//! ┌─────────────────────────────────────┐
//! │  Packager / Installer (High-level)  │
//! ├─────────────────────────────────────┤
//! │  Package Format / Compression       │
//! ├─────────────────────────────────────┤
//! │  Filesystem / Platform Abstraction  │
//! └─────────────────────────────────────┘
//! ```
//!
//! ## Platform Support
//!
//! Currently supports Windows with full functionality:
//! - Registry operations
//! - Desktop shortcuts
//! - Auto-startup configuration
//! - UAC privilege elevation
//!
//! The platform abstraction trait allows future expansion to other platforms.

pub mod components;
pub mod compression;
pub mod exe_builder;
pub mod filesystem;
pub mod flow_executor;
pub mod installer;
pub mod localization;
pub mod logging;
pub mod package;
pub mod packager;
pub mod platform;
pub mod ui_resources;
pub mod uninstall;

// Re-export compression functions
pub use components::{
    download_component_to_cache, find_component, load_component_manifest, sha256_file_hex,
    verify_component_sha256, verify_component_signature, ComponentDownloadPolicy, ComponentEntry,
    ComponentManifest, ComponentPackage, ComponentSignaturePolicy,
};
pub use compression::{calculate_crc32, compress, decompress, verify_crc32};

// Re-export filesystem types and functions
pub use filesystem::{
    calculate_block_count, calculate_required_space, check_disk_space,
    check_disk_space_with_default_buffer, create_dir_all, delete_dir_all, delete_empty_dir,
    delete_file, divide_into_blocks, get_available_space, get_disk_space_info, scan_directory,
    scan_directory_with_options, write_file, write_file_with_mode, BlockData, BlockDivisionResult,
    BlockInfo, DiskSpaceInfo, FileInfo, ScanOptions, DEFAULT_DISK_SPACE_BUFFER,
};

// Re-export package types and functions
pub use package::{
    read_footer, read_header, read_metadata, read_toc, write_footer, write_header, write_metadata,
    write_toc, Toc,
};

// Re-export platform trait
pub use platform::{create_platform, Platform, UninstallInfo, WindowsPlatform};

// Re-export packager and installer
pub use installer::{Installer, ScriptPolicy};
pub use packager::{CompressedBlock, PackageStats, Packager};

// Re-export uninstaller
pub use uninstall::{InstallManifest, UninstallStats, Uninstaller};

// Re-export UI resources
pub use ui_resources::UIResources;

// Re-export localization
pub use localization::LocalizationManager;

// Re-export logging
pub use logging::{init_cli_logging, init_gui_logging, init_logging};

// Re-export exe builder
pub use exe_builder::{
    build_self_contained_installer, build_self_contained_installer_from_memory,
    check_embedded_package, extract_embedded_package, read_embedded_package, OverlayMarker,
    OVERLAY_MAGIC, OVERLAY_MARKER_SIZE,
};

// Re-export flow executor
pub use flow_executor::{ExecutionReport, FlowContext, FlowExecutor, FlowRuntime};

// Re-export shared types for convenience
pub use installer_shared::*;
