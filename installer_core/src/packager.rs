//! Packager module for creating installer packages.
//!
//! This module provides the core packaging functionality for creating installer packages.
//! It handles:
//! - Directory scanning with folder_targets support
//! - Parallel block compression using rayon
//! - TOC and metadata generation
//! - Complete package assembly
//!
//! # Requirements
//! - 2.1: Recursive directory scanning
//! - 2.2: File information collection
//! - 2.6: TOC generation with checksums
//! - 2.7: Metadata serialization to MessagePack
//! - 2.10: Parallel block compression
//! - 13.1, 13.2, 13.3: Thread pool for parallel compression with order preservation

use crate::compression::{calculate_crc32, compress};
use crate::filesystem::{
    divide_into_blocks, scan_directory_with_options, BlockDivisionResult, FileInfo, ScanOptions,
};
use crate::package::{write_footer, write_header, write_toc, Toc};
use crate::platform::{create_platform, Platform};
use crate::ui_resources::UIResources;
use installer_shared::{
    BlockEntry, CompressionAlgorithm, FileEntry, InstallerError, PackageFooter, PackageHeader,
    PackageMetadata, PackagerConfig, Phase, ProgressEvent, Result, TocHeader,
};
use rayon::prelude::*;
use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use tracing::info;

/// Statistics about a built package.
#[derive(Debug, Clone)]
pub struct PackageStats {
    /// Total number of files
    pub total_files: usize,
    /// Total uncompressed size
    pub total_size: u64,
    /// Total compressed size
    pub compressed_size: u64,
    /// Compression ratio (compressed / original)
    pub compression_ratio: f64,
}

/// Packager for creating installer packages.
///
/// The Packager handles the complete workflow of creating an installer package:
/// 1. Scanning input directories for files
/// 2. Dividing files into blocks
/// 3. Compressing blocks in parallel
/// 4. Generating TOC and metadata
/// 5. Writing the complete package
///
/// # Example
/// ```no_run
/// use installer_core::packager::Packager;
/// use installer_shared::{PackagerConfig, Phase, ProgressEvent};
/// use std::path::Path;
///
/// let config = PackagerConfig::default();
/// let packager = Packager::new(config).unwrap();
///
/// let stats = packager.build_package(
///     Path::new("./input"),
///     Path::new("./output.pkg"),
///     None,
///     |event| println!("Progress: {:?}", event),
/// ).unwrap();
/// ```
pub struct Packager {
    config: PackagerConfig,
    #[allow(dead_code)]
    platform: Box<dyn Platform>,
}

impl Packager {
    /// Create a new packager with the given configuration.
    ///
    /// # Arguments
    /// * `config` - The packager configuration
    ///
    /// # Returns
    /// A new Packager instance
    ///
    /// # Requirements
    /// - 2.1: Initialize packager with configuration
    pub fn new(config: PackagerConfig) -> Result<Self> {
        info!(
            "Creating packager for '{}' v{}",
            config.application_name, config.version
        );
        Ok(Self {
            config,
            platform: create_platform(),
        })
    }

    /// Get a reference to the packager configuration.
    pub fn config(&self) -> &PackagerConfig {
        &self.config
    }

    /// Scan the input directory for files.
    ///
    /// This method recursively scans the input directory and collects file information.
    /// It applies folder_targets configuration to map source folders to target directories.
    ///
    /// # Arguments
    /// * `input_dir` - The directory to scan
    ///
    /// # Returns
    /// A vector of FileInfo structures for all discovered files
    ///
    /// # Requirements
    /// - 2.1: Recursive directory scanning
    /// - 2.2: Collect file path, size, and permission information
    pub fn scan_directory(&self, input_dir: &Path) -> Result<Vec<FileInfo>> {
        info!("Scanning directory: {:?}", input_dir);
        
        // Create scan options from config - exclude packager config files at root level only
        let options = ScanOptions {
            skip_hidden: false,
            skip_system: false,
            exclude_patterns: vec![
                "packager.json".to_string(),  // Only matches root level packager.json
            ],
            follow_symlinks: false,
        };
        
        let mut files = scan_directory_with_options(input_dir, &options)?;
        
        // Filter out root-level icon file (app.ico) that's used for installer icon
        files.retain(|f| f.relative_path != "app.ico");
        
        // Apply folder_targets configuration to remap paths
        if !self.config.folder_targets.is_empty() {
            files = self.apply_folder_targets(files);
        }
        
        info!("Found {} files", files.len());
        Ok(files)
    }

    /// Apply folder_targets configuration to remap file paths.
    ///
    /// This allows mapping source folders to different target directories
    /// during installation. The target_directory can contain environment
    /// variables like %AppData% which will be expanded during installation.
    fn apply_folder_targets(&self, files: Vec<FileInfo>) -> Vec<FileInfo> {
        files
            .into_iter()
            .filter_map(|mut file| {
                for target in &self.config.folder_targets {
                    // Check if this file is in a folder that should be redirected
                    if file.relative_path.starts_with(&target.folder_name) 
                        || file.relative_path.starts_with(&format!("{}/", target.folder_name)) {
                        // Replace the folder name with the target directory
                        // The target_directory may contain environment variables like %AppData%
                        file.relative_path = file
                            .relative_path
                            .replacen(&target.folder_name, &target.target_directory, 1);
                        return Some(file);
                    }
                }
                // Files not in folder_targets go to the install directory
                Some(file)
            })
            .collect()
    }

    /// Compress files into blocks.
    ///
    /// This method divides files into blocks of configurable size and compresses
    /// them in parallel using rayon. Block order is preserved despite parallel
    /// processing by sorting blocks by ID after compression.
    ///
    /// # Arguments
    /// * `files` - The files to compress
    /// * `progress` - A callback function to receive progress events
    ///
    /// # Returns
    /// A vector of CompressedBlock structures
    ///
    /// # Requirements
    /// - 2.3: Divide data into configurable block sizes (default 4MB)
    /// - 2.4: Use Zstd compression with configurable level
    /// - 2.10: Parallel block compression using thread pool
    /// - 13.1, 13.2: Thread pool for parallel compression
    /// - 13.3: Maintain block order in output
    pub fn compress_blocks<F>(
        &self,
        files: &[FileInfo],
        progress: F,
    ) -> Result<Vec<CompressedBlock>>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        let division = divide_into_blocks(files, self.config.block_size)?;
        self.compress_block_data(&division, progress)
    }

    fn compress_block_data<F>(
        &self,
        division: &BlockDivisionResult,
        progress: F,
    ) -> Result<Vec<CompressedBlock>>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        let algorithm = self.config.compression_algorithm;
        let level = self.config.compression_level as i32;

        info!(
            "Compressing {} blocks with {:?} level {}",
            division.block_data.len(),
            algorithm,
            level
        );

        let total_blocks = division.block_data.len() as u64;
        let processed = Arc::new(AtomicU64::new(0));

        // Map block index -> file list for metadata/debug
        let mut block_files: Vec<Vec<(String, u64)>> =
            vec![Vec::new(); division.block_data.len()];
        for block_info in &division.block_infos {
            let files = block_info
                .file_indices
                .iter()
                .filter_map(|idx| division.file_entries.get(*idx))
                .map(|entry| (entry.path.clone(), entry.original_size))
                .collect::<Vec<_>>();
            if let Some(slot) = block_files.get_mut(block_info.index as usize) {
                *slot = files;
            }
        }

        // Configure thread pool if specified
        let pool = if let Some(thread_count) = self.config.thread_count {
            rayon::ThreadPoolBuilder::new()
                .num_threads(thread_count)
                .build()
                .ok()
        } else {
            None
        };

        let compress_fn = |block_data: &Vec<crate::filesystem::BlockData>| {
            block_data
                .par_iter()
                .map(|block| {
                    let original_size = block.data.len() as u64;
                    let compressed = compress(&block.data, algorithm, level)?;
                    let compressed_size = compressed.len() as u64;
                    let checksum = calculate_crc32(&compressed);

                    let count = processed.fetch_add(1, Ordering::SeqCst) + 1;
                    progress(ProgressEvent::new(Phase::Compressing, count, total_blocks));

                    let files = block_files
                        .get(block.index as usize)
                        .cloned()
                        .unwrap_or_default();

                    Ok(CompressedBlock {
                        id: block.index,
                        data: compressed,
                        original_size,
                        compressed_size,
                        checksum,
                        algorithm,
                        files,
                    })
                })
                .collect::<Vec<Result<CompressedBlock>>>()
        };

        let compressed_blocks: Vec<Result<CompressedBlock>> = if let Some(pool) = pool {
            pool.install(|| compress_fn(&division.block_data))
        } else {
            compress_fn(&division.block_data)
        };

        let mut blocks: Vec<CompressedBlock> = compressed_blocks
            .into_iter()
            .collect::<Result<Vec<_>>>()?;

        blocks.sort_by_key(|b| b.id);
        info!("Compression complete: {} blocks", blocks.len());
        Ok(blocks)
    }

    /// Generate TOC from compressed blocks.
    ///
    /// Creates the Table of Contents structure from the compressed blocks,
    /// including file entries with block mappings and block entries with
    /// compression metadata.
    ///
    /// # Arguments
    /// * `blocks` - The compressed blocks
    ///
    /// # Returns
    /// A Toc structure ready for serialization
    ///
    /// # Requirements
    /// - 2.6: Generate TOC with file entries and block entries including checksums
    pub fn generate_toc(
        &self,
        file_entries: Vec<FileEntry>,
        blocks: &[CompressedBlock],
    ) -> Result<Toc> {
        let mut block_entries = Vec::new();
        let mut current_offset = 0u64;

        for block in blocks.iter() {
            block_entries.push(BlockEntry {
                offset: current_offset,
                compressed_size: block.compressed_size,
                original_size: block.original_size,
                checksum: block.checksum,
                algorithm: block.algorithm,
            });

            current_offset += block.compressed_size;
        }

        info!(
            "Generated TOC: {} files, {} blocks",
            file_entries.len(),
            block_entries.len()
        );

        Ok(Toc {
            header: TocHeader {
                file_count: file_entries.len() as u32,
                block_count: block_entries.len() as u32,
                toc_version: 1,
                reserved: 0,
            },
            files: file_entries,
            blocks: block_entries,
        })
    }

    /// Generate metadata from configuration.
    ///
    /// Creates the PackageMetadata structure from the packager configuration,
    /// ready for MessagePack serialization.
    ///
    /// # Arguments
    /// * `ui_resources_checksum` - Optional CRC32 checksum of embedded UI resources
    ///
    /// # Returns
    /// A PackageMetadata structure
    ///
    /// # Requirements
    /// - 2.7: Generate metadata and serialize to MessagePack format
    pub fn generate_metadata(&self, ui_resources_checksum: Option<u32>) -> Result<PackageMetadata> {
        info!("Generating metadata for '{}'", self.config.application_name);
        
        Ok(PackageMetadata {
            app_name: self.config.application_name.clone(),
            version: self.config.version.clone(),
            default_install_dir: self.config.default_install_dir.clone(),
            vendor: self.config.vendor.clone(),
            license_text: self.config.license_text.clone(),
            require_admin: self.config.require_admin,
            icon_path: self.config.icon_path.clone(),
            ui_theme: None,
            min_windows_version: self.config.min_windows_version,
            registry_entries: self.config.registry_entries.clone(),
            auto_startup: self.config.auto_startup,
            desktop_icons: self.config.desktop_icons,
            process_name: self.config.process_name.clone(),
            ui_resources_checksum,
        })
    }

    /// Embed UI resources from a directory.
    ///
    /// Scans the UI resources directory and creates a compressed archive
    /// for embedding into the package.
    ///
    /// # Arguments
    /// * `ui_dir` - Path to the UI resources directory
    ///
    /// # Returns
    /// UIResources containing the compressed archive
    ///
    /// # Requirements
    /// - 5.1: Accept ui_resources directory path
    /// - 5.2: Scan HTML, CSS, JavaScript, and image files
    /// - 5.3: Compress as tar.gz archive
    pub fn embed_ui_resources(&self, ui_dir: &Path) -> Result<UIResources> {
        info!("Embedding UI resources from {:?}", ui_dir);
        UIResources::from_directory(ui_dir)
    }

    /// Build a complete package.
    ///
    /// This is the main entry point for package creation. It coordinates all
    /// packaging steps:
    /// 1. Scan input directory for files
    /// 2. Compress files into blocks
    /// 3. Generate TOC and metadata
    /// 4. Optionally embed UI resources
    /// 5. Write complete package to output file
    ///
    /// # Arguments
    /// * `input_dir` - The directory containing files to package
    /// * `output_path` - The path for the output package file
    /// * `ui_resources_dir` - Optional directory containing UI resources
    /// * `progress` - A callback function to receive progress events
    ///
    /// # Returns
    /// PackageStats with information about the created package
    ///
    /// # Requirements
    /// - 1.1: Package format with Header, TOC, Metadata, Data Blocks, Footer
    /// - 2.8: Write package in correct order
    /// - 2.9: Report compression statistics
    /// - 5.3: Embed UI resources as compressed archive
    pub fn build_package<F>(
        &self,
        input_dir: &Path,
        output_path: &Path,
        ui_resources_dir: Option<&Path>,
        progress: F,
    ) -> Result<PackageStats>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        info!("Building package from {:?} to {:?}", input_dir, output_path);

        // Phase 1: Scan files
        progress(ProgressEvent::new(Phase::Scanning, 0, 1));
        let files = self.scan_directory(input_dir)?;
        let total_files = files.len();
        let total_size: u64 = files.iter().map(|f| f.size).sum();
        progress(ProgressEvent::new(Phase::Scanning, 1, 1));

        if total_files == 0 {
            return Err(InstallerError::Config(
                "No files found in input directory".to_string(),
            ));
        }

        // Phase 2: Divide into blocks and compress
        let division = divide_into_blocks(&files, self.config.block_size)?;
        let file_entries = division.file_entries.clone();
        let blocks = self.compress_block_data(&division, &progress)?;

        // Phase 3: Embed UI resources if provided
        let ui_resources = if let Some(ui_dir) = ui_resources_dir {
            info!("Embedding UI resources from {:?}", ui_dir);
            Some(self.embed_ui_resources(ui_dir)?)
        } else {
            None
        };

        // Phase 4: Generate TOC and metadata
        let toc = self.generate_toc(file_entries, &blocks)?;
        let ui_checksum = ui_resources.as_ref().map(|r| r.checksum);
        let metadata = self.generate_metadata(ui_checksum)?;

        // Serialize TOC with checksum wrapper to get the actual size
        let mut toc_data = Vec::new();
        let toc_size = write_toc(&mut toc_data, &toc)? as u64;
        
        // Serialize metadata
        let metadata_data = rmp_serde::to_vec(&metadata)
            .map_err(|e| InstallerError::Serialization(e.to_string()))?;

        // Calculate offsets (Header -> TOC -> Metadata -> Data -> UI Resources -> Footer)
        let header_size = std::mem::size_of::<PackageHeader>() as u64;
        let toc_offset = header_size;
        let metadata_offset = toc_offset + toc_size;
        let metadata_size = metadata_data.len() as u64;
        let data_offset = metadata_offset + metadata_size;
        let data_size: u64 = blocks.iter().map(|b| b.compressed_size).sum();

        // UI resources offset and size
        let (ui_resources_offset, ui_resources_size) = if let Some(ref ui) = ui_resources {
            (data_offset + data_size, ui.archive.len() as u64)
        } else {
            (0, 0)
        };

        // Create header with all offsets
        let mut header = PackageHeader {
            toc_offset,
            toc_size,
            metadata_offset,
            metadata_size,
            data_offset,
            data_size,
            ui_resources_offset,
            ui_resources_size,
            ..Default::default()
        };

        // Set UI resources flag if present
        if ui_resources.is_some() {
            header.set_ui_resources_flag(true);
        }

        // Phase 5: Write package
        progress(ProgressEvent::new(Phase::Writing, 0, 1));
        let file = File::create(output_path)?;
        let mut writer = BufWriter::new(file);
        let mut hasher = crc32fast::Hasher::new();

        let mut write_and_hash = |data: &[u8]| -> Result<()> {
            writer.write_all(data)?;
            hasher.update(data);
            Ok(())
        };

        // Write in order: Header -> TOC -> Metadata -> Data Blocks -> UI Resources -> Footer
        let mut header_data = Vec::new();
        write_header(&mut header_data, &header)?;
        write_and_hash(&header_data)?;
        write_and_hash(&toc_data)?;
        write_and_hash(&metadata_data)?;

        for block in &blocks {
            write_and_hash(&block.data)?;
        }

        // Write UI resources if present
        if let Some(ref ui) = ui_resources {
            write_and_hash(&ui.archive)?;
            info!(
                "Embedded UI resources: {} bytes (checksum: {:08x})",
                ui.archive.len(),
                ui.checksum
            );
        }

        // Write footer for quick section location (CRC32 over all bytes before footer)
        let crc32 = hasher.finalize();
        let footer = PackageFooter {
            header_offset: 0,
            toc_offset,
            metadata_offset,
            data_offset,
            ui_resources_offset,
            crc32,
            ..Default::default()
        };
        write_footer(&mut writer, &footer)?;

        writer.flush()?;
        progress(ProgressEvent::new(Phase::Writing, 1, 1));

        // Calculate statistics
        let compressed_size = data_size + toc_size + metadata_size + header_size + ui_resources_size;
        let compression_ratio = if total_size > 0 {
            compressed_size as f64 / total_size as f64
        } else {
            1.0
        };

        info!(
            "Package built: {} files, {} bytes -> {} bytes ({:.1}%)",
            total_files,
            total_size,
            compressed_size,
            compression_ratio * 100.0
        );

        // Phase 6: Complete
        progress(ProgressEvent::new(Phase::Completing, 1, 1));

        Ok(PackageStats {
            total_files,
            total_size,
            compressed_size,
            compression_ratio,
        })
    }

    /// Build a self-contained installer executable.
    ///
    /// This is the main entry point for creating a distributable installer.
    /// It creates a single executable file that contains both the installer
    /// logic and the package data, which users can run directly.
    ///
    /// # Arguments
    /// * `input_dir` - The directory containing files to package
    /// * `template_exe` - Path to the installer template executable (installer_gui.exe)
    /// * `output_exe` - The path for the output installer executable (.exe)
    /// * `ui_resources_dir` - Optional directory containing UI resources
    /// * `progress` - A callback function to receive progress events
    ///
    /// # Returns
    /// PackageStats with information about the created installer
    ///
    /// # Requirements
    /// - Creates a single .exe that users can run directly
    /// - Embeds package data into the installer executable
    /// - 5.3: Embed UI resources as compressed archive
    pub fn build_installer<F>(
        &self,
        input_dir: &Path,
        template_exe: &Path,
        output_exe: &Path,
        ui_resources_dir: Option<&Path>,
        progress: F,
    ) -> Result<PackageStats>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        use crate::exe_builder::build_self_contained_installer_from_memory;

        info!(
            "Building self-contained installer from {:?} to {:?}",
            input_dir, output_exe
        );

        // Phase 1: Scan files
        progress(ProgressEvent::new(Phase::Scanning, 0, 1));
        let files = self.scan_directory(input_dir)?;
        let total_files = files.len();
        let total_size: u64 = files.iter().map(|f| f.size).sum();
        progress(ProgressEvent::new(Phase::Scanning, 1, 1));

        if total_files == 0 {
            return Err(InstallerError::Config(
                "No files found in input directory".to_string(),
            ));
        }

        // Phase 2: Divide into blocks and compress
        let division = divide_into_blocks(&files, self.config.block_size)?;
        let file_entries = division.file_entries.clone();
        let blocks = self.compress_block_data(&division, &progress)?;

        // Phase 3: Embed UI resources if provided
        let ui_resources = if let Some(ui_dir) = ui_resources_dir {
            info!("Embedding UI resources from {:?}", ui_dir);
            Some(self.embed_ui_resources(ui_dir)?)
        } else {
            None
        };

        // Phase 4: Generate TOC and metadata
        let toc = self.generate_toc(file_entries, &blocks)?;
        let ui_checksum = ui_resources.as_ref().map(|r| r.checksum);
        let metadata = self.generate_metadata(ui_checksum)?;

        // Serialize TOC with checksum wrapper
        let mut toc_data = Vec::new();
        let toc_size = write_toc(&mut toc_data, &toc)? as u64;

        // Serialize metadata
        let metadata_data = rmp_serde::to_vec(&metadata)
            .map_err(|e| InstallerError::Serialization(e.to_string()))?;

        // Calculate offsets
        let header_size = std::mem::size_of::<PackageHeader>() as u64;
        let toc_offset = header_size;
        let metadata_offset = toc_offset + toc_size;
        let metadata_size = metadata_data.len() as u64;
        let data_offset = metadata_offset + metadata_size;
        let data_size: u64 = blocks.iter().map(|b| b.compressed_size).sum();

        let (ui_resources_offset, ui_resources_size) = if let Some(ref ui) = ui_resources {
            (data_offset + data_size, ui.archive.len() as u64)
        } else {
            (0, 0)
        };

        // Create header
        let mut header = PackageHeader {
            toc_offset,
            toc_size,
            metadata_offset,
            metadata_size,
            data_offset,
            data_size,
            ui_resources_offset,
            ui_resources_size,
            ..Default::default()
        };

        if ui_resources.is_some() {
            header.set_ui_resources_flag(true);
        }

        // Phase 5: Build package data in memory
        progress(ProgressEvent::new(Phase::Writing, 0, 1));

        let mut package_data = Vec::new();

        // Write package format to memory buffer
        // Write header
        write_header(&mut package_data, &header)?;
        
        // Write TOC
        package_data.extend_from_slice(&toc_data);
        
        // Write metadata
        package_data.extend_from_slice(&metadata_data);

        // Write compressed data blocks
        for block in &blocks {
            package_data.extend_from_slice(&block.data);
        }

        // Write UI resources if present
        if let Some(ref ui) = ui_resources {
            package_data.extend_from_slice(&ui.archive);
        }

        // Write footer (CRC32 over all bytes before footer)
        let crc32 = crate::package::calculate_crc32(&package_data);
        let footer = PackageFooter {
            header_offset: 0,
            toc_offset,
            metadata_offset,
            data_offset,
            ui_resources_offset,
            crc32,
            ..Default::default()
        };
        write_footer(&mut package_data, &footer)?;

        // Phase 6: Embed package data into installer executable
        let installer_size = build_self_contained_installer_from_memory(
            template_exe,
            &package_data,
            output_exe,
        )?;

        progress(ProgressEvent::new(Phase::Writing, 1, 1));

        // Calculate statistics
        let compressed_size = package_data.len() as u64;
        let compression_ratio = if total_size > 0 {
            compressed_size as f64 / total_size as f64
        } else {
            1.0
        };

        info!(
            "Self-contained installer built: {} files, {} bytes -> {} bytes (installer: {} bytes)",
            total_files, total_size, compressed_size, installer_size
        );

        // Phase 7: Complete
        progress(ProgressEvent::new(Phase::Completing, 1, 1));

        Ok(PackageStats {
            total_files,
            total_size,
            compressed_size,
            compression_ratio,
        })
    }
}

/// A compressed block of data.
#[derive(Debug)]
pub struct CompressedBlock {
    /// Block ID for ordering
    pub id: u32,
    /// Compressed data
    pub data: Vec<u8>,
    /// Original uncompressed size
    pub original_size: u64,
    /// Compressed size
    pub compressed_size: u64,
    /// CRC32 checksum of compressed data
    pub checksum: u32,
    /// Compression algorithm used
    pub algorithm: CompressionAlgorithm,
    /// Files contained in this block (path, size)
    pub files: Vec<(String, u64)>,
}


#[cfg(test)]
mod tests {
    use super::*;
    use installer_shared::PackagerConfig;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn test_packager_new() {
        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        assert_eq!(packager.config().application_name, "MyApp");
    }

    #[test]
    fn test_scan_directory() {
        let dir = tempdir().unwrap();
        let file1 = dir.path().join("file1.txt");
        let file2 = dir.path().join("file2.txt");
        fs::write(&file1, b"content1").unwrap();
        fs::write(&file2, b"content2").unwrap();

        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        let files = packager.scan_directory(dir.path()).unwrap();

        assert_eq!(files.len(), 2);
    }

    #[test]
    fn test_compress_blocks() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("test.txt");
        fs::write(&file, b"Hello, World!").unwrap();

        let config = PackagerConfig {
            block_size: 1024,
            ..Default::default()
        };
        let packager = Packager::new(config).unwrap();
        let files = packager.scan_directory(dir.path()).unwrap();
        let blocks = packager.compress_blocks(&files, |_| {}).unwrap();

        assert_eq!(blocks.len(), 1);
        assert!(blocks[0].compressed_size > 0);
    }

    #[test]
    fn test_generate_toc() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("test.txt");
        fs::write(&file, b"Hello, World!").unwrap();

        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        let files = packager.scan_directory(dir.path()).unwrap();
        let division = divide_into_blocks(&files, packager.config().block_size).unwrap();
        let blocks = packager.compress_blocks(&files, |_| {}).unwrap();
        let toc = packager.generate_toc(division.file_entries, &blocks).unwrap();

        assert_eq!(toc.header.file_count, 1);
        assert_eq!(toc.header.block_count, 1);
    }

    #[test]
    fn test_generate_metadata() {
        let config = PackagerConfig {
            application_name: "TestApp".to_string(),
            version: "1.0.0".to_string(),
            ..Default::default()
        };
        let packager = Packager::new(config).unwrap();
        let metadata = packager.generate_metadata(None).unwrap();

        assert_eq!(metadata.app_name, "TestApp");
        assert_eq!(metadata.version, "1.0.0");
        assert!(metadata.ui_resources_checksum.is_none());
    }

    #[test]
    fn test_generate_metadata_with_ui_checksum() {
        let config = PackagerConfig {
            application_name: "TestApp".to_string(),
            version: "1.0.0".to_string(),
            ..Default::default()
        };
        let packager = Packager::new(config).unwrap();
        let metadata = packager.generate_metadata(Some(0xDEADBEEF)).unwrap();

        assert_eq!(metadata.app_name, "TestApp");
        assert_eq!(metadata.ui_resources_checksum, Some(0xDEADBEEF));
    }

    #[test]
    fn test_build_package() {
        let input_dir = tempdir().unwrap();
        let output_dir = tempdir().unwrap();
        let file = input_dir.path().join("test.txt");
        fs::write(&file, b"Hello, World!").unwrap();

        let output_path = output_dir.path().join("test.pkg");

        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        let stats = packager
            .build_package(input_dir.path(), &output_path, None, |_| {})
            .unwrap();

        assert_eq!(stats.total_files, 1);
        assert!(output_path.exists());
    }

    #[test]
    fn test_build_package_with_ui_resources() {
        let input_dir = tempdir().unwrap();
        let output_dir = tempdir().unwrap();
        let ui_dir = tempdir().unwrap();

        // Create input file
        let file = input_dir.path().join("test.txt");
        fs::write(&file, b"Hello, World!").unwrap();

        // Create UI resources
        fs::write(ui_dir.path().join("index.html"), "<html></html>").unwrap();
        let locales_dir = ui_dir.path().join("locales");
        fs::create_dir_all(&locales_dir).unwrap();
        fs::write(locales_dir.join("en-US.json"), r#"{"key": "value"}"#).unwrap();

        let output_path = output_dir.path().join("test.pkg");

        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        let stats = packager
            .build_package(input_dir.path(), &output_path, Some(ui_dir.path()), |_| {})
            .unwrap();

        assert_eq!(stats.total_files, 1);
        assert!(output_path.exists());
        // Package should be larger due to UI resources
        assert!(stats.compressed_size > 0);
    }

    #[test]
    fn test_folder_targets() {
        let dir = tempdir().unwrap();
        let subdir = dir.path().join("src");
        fs::create_dir(&subdir).unwrap();
        let file = subdir.join("main.rs");
        fs::write(&file, b"fn main() {}").unwrap();

        let config = PackagerConfig {
            folder_targets: vec![installer_shared::FolderTarget {
                folder_name: "src".to_string(),
                target_directory: "bin".to_string(),
            }],
            ..Default::default()
        };
        let packager = Packager::new(config).unwrap();
        let files = packager.scan_directory(dir.path()).unwrap();

        assert_eq!(files.len(), 1);
        assert!(files[0].relative_path.starts_with("bin"));
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use installer_shared::{CompressionAlgorithm, PackagerConfig};
    use proptest::prelude::*;
    use std::fs;
    use tempfile::tempdir;

    /// Strategy for generating file content
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 100..5000)
    }

    /// Strategy for generating multiple files
    fn multiple_files_strategy() -> impl Strategy<Value = Vec<Vec<u8>>> {
        prop::collection::vec(file_content_strategy(), 2..10)
    }

    /// Strategy for block sizes
    fn block_size_strategy() -> impl Strategy<Value = usize> {
        500usize..5000
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// **Property 5: Parallel Compression Order Preservation**
        /// For any file set, parallel compression should produce the same block
        /// sequence as serial compression would.
        ///
        /// **Validates: Requirements 13.3**
        #[test]
        fn prop_parallel_compression_order_preserved(
            file_contents in multiple_files_strategy(),
            block_size in block_size_strategy()
        ) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create files with unique names
            for (i, content) in file_contents.iter().enumerate() {
                let file_path = dir.path().join(format!("file_{:04}.bin", i));
                fs::write(&file_path, content).expect("Failed to write file");
            }

            let config = PackagerConfig {
                block_size,
                compression_algorithm: CompressionAlgorithm::Zstd,
                compression_level: 1, // Fast compression for tests
                ..Default::default()
            };

            let packager = Packager::new(config).expect("Failed to create packager");
            let files = packager.scan_directory(dir.path()).expect("Failed to scan");

            // Compress blocks (parallel)
            let blocks = packager.compress_blocks(&files, |_| {}).expect("Failed to compress");

            // Property 1: Block IDs should be sequential starting from 0
            for (i, block) in blocks.iter().enumerate() {
                prop_assert_eq!(
                    block.id as usize,
                    i,
                    "Block IDs should be sequential: expected {}, got {}",
                    i,
                    block.id
                );
            }

            // Property 2: Files should appear in the same order as scanned
            let mut file_order_in_blocks: Vec<String> = Vec::new();
            for block in &blocks {
                for (path, _) in &block.files {
                    file_order_in_blocks.push(path.clone());
                }
            }

            // Deduplicate consecutive duplicates (files spanning multiple blocks)
            let mut deduped_order: Vec<String> = Vec::new();
            for path in file_order_in_blocks {
                if deduped_order.last().map(|p| p == &path).unwrap_or(false) {
                    continue;
                }
                deduped_order.push(path);
            }

            let scanned_order: Vec<String> = files.iter().map(|f| f.relative_path.clone()).collect();
            prop_assert_eq!(
                deduped_order,
                scanned_order,
                "File order in blocks should match scanned order"
            );

            // Property 3: All blocks should have valid checksums
            for block in &blocks {
                let computed_checksum = crate::compression::calculate_crc32(&block.data);
                prop_assert_eq!(
                    block.checksum,
                    computed_checksum,
                    "Block checksum should match computed value"
                );
            }
        }

        /// **Property 5 (continued): Compression Determinism**
        /// Running compression twice on the same input should produce identical results.
        ///
        /// **Validates: Requirements 13.3**
        #[test]
        fn prop_compression_deterministic(
            file_contents in multiple_files_strategy(),
            block_size in block_size_strategy()
        ) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create files
            for (i, content) in file_contents.iter().enumerate() {
                let file_path = dir.path().join(format!("file_{:04}.bin", i));
                fs::write(&file_path, content).expect("Failed to write file");
            }

            let config = PackagerConfig {
                block_size,
                compression_algorithm: CompressionAlgorithm::Zstd,
                compression_level: 1,
                ..Default::default()
            };

            let packager = Packager::new(config).expect("Failed to create packager");
            let files = packager.scan_directory(dir.path()).expect("Failed to scan");

            // Compress twice
            let blocks1 = packager.compress_blocks(&files, |_| {}).expect("First compression failed");
            let blocks2 = packager.compress_blocks(&files, |_| {}).expect("Second compression failed");

            // Property: Both compressions should produce identical results
            prop_assert_eq!(
                blocks1.len(),
                blocks2.len(),
                "Block count should be identical"
            );

            for (b1, b2) in blocks1.iter().zip(blocks2.iter()) {
                prop_assert_eq!(b1.id, b2.id, "Block IDs should match");
                prop_assert_eq!(b1.original_size, b2.original_size, "Original sizes should match");
                prop_assert_eq!(b1.compressed_size, b2.compressed_size, "Compressed sizes should match");
                prop_assert_eq!(b1.checksum, b2.checksum, "Checksums should match");
                prop_assert_eq!(&b1.data, &b2.data, "Compressed data should be identical");
            }
        }

        /// **Property 5 (continued): Block Order Independence from Thread Count**
        /// Block order should be the same regardless of thread count configuration.
        ///
        /// **Validates: Requirements 13.3**
        #[test]
        fn prop_block_order_independent_of_threads(
            file_contents in prop::collection::vec(file_content_strategy(), 3..8),
            block_size in 500usize..2000
        ) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create files
            for (i, content) in file_contents.iter().enumerate() {
                let file_path = dir.path().join(format!("file_{:04}.bin", i));
                fs::write(&file_path, content).expect("Failed to write file");
            }

            // Config with 1 thread
            let config1 = PackagerConfig {
                block_size,
                compression_algorithm: CompressionAlgorithm::Zstd,
                compression_level: 1,
                thread_count: Some(1),
                ..Default::default()
            };

            // Config with multiple threads
            let config2 = PackagerConfig {
                block_size,
                compression_algorithm: CompressionAlgorithm::Zstd,
                compression_level: 1,
                thread_count: Some(4),
                ..Default::default()
            };

            let packager1 = Packager::new(config1).expect("Failed to create packager 1");
            let packager2 = Packager::new(config2).expect("Failed to create packager 2");

            let files1 = packager1.scan_directory(dir.path()).expect("Failed to scan 1");
            let files2 = packager2.scan_directory(dir.path()).expect("Failed to scan 2");

            let blocks1 = packager1.compress_blocks(&files1, |_| {}).expect("Compression 1 failed");
            let blocks2 = packager2.compress_blocks(&files2, |_| {}).expect("Compression 2 failed");

            // Property: Block order should be identical
            prop_assert_eq!(
                blocks1.len(),
                blocks2.len(),
                "Block count should be identical regardless of thread count"
            );

            for (b1, b2) in blocks1.iter().zip(blocks2.iter()) {
                prop_assert_eq!(b1.id, b2.id, "Block IDs should match");
                
                // File order within blocks should match
                let files1: Vec<&str> = b1.files.iter().map(|(p, _)| p.as_str()).collect();
                let files2: Vec<&str> = b2.files.iter().map(|(p, _)| p.as_str()).collect();
                prop_assert_eq!(files1, files2, "File order in blocks should match");
            }
        }
    }
}
