//! File system operations module.
//!
//! Provides file scanning, directory operations, disk space checking,
//! and block division for the installer system.

use installer_shared::{FileEntry, InstallerError, Result};
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::SystemTime;
use tracing::{debug, warn};
use walkdir::WalkDir;

#[cfg(not(windows))]
use std::fs::Permissions;

/// Default buffer size for disk space check (100MB).
pub const DEFAULT_DISK_SPACE_BUFFER: u64 = 100 * 1024 * 1024;

/// Information about a file.
#[derive(Debug, Clone, PartialEq)]
pub struct FileInfo {
    /// Absolute path to the file
    pub path: PathBuf,
    /// Relative path from the scan root
    pub relative_path: String,
    /// File size in bytes
    pub size: u64,
    /// File permissions/mode
    pub mode: u32,
    /// Last modification time
    pub modified: SystemTime,
}

/// Options for directory scanning.
#[derive(Debug, Clone)]
pub struct ScanOptions {
    /// Whether to skip hidden files (files starting with '.')
    pub skip_hidden: bool,
    /// Whether to skip system files (Windows-specific)
    pub skip_system: bool,
    /// File patterns to exclude (glob patterns)
    pub exclude_patterns: Vec<String>,
    /// Follow symbolic links
    pub follow_symlinks: bool,
}

impl Default for ScanOptions {
    fn default() -> Self {
        Self {
            skip_hidden: false,
            skip_system: false,
            exclude_patterns: Vec::new(),
            follow_symlinks: false,
        }
    }
}

/// Scan a directory recursively and collect file information.
/// Uses default options (includes all files).
pub fn scan_directory(root: &Path) -> Result<Vec<FileInfo>> {
    scan_directory_with_options(root, &ScanOptions::default())
}

/// Scan a directory recursively with custom options.
///
/// # Arguments
/// * `root` - The root directory to scan
/// * `options` - Scanning options for filtering files
///
/// # Returns
/// A vector of FileInfo sorted by relative path for consistent ordering.
pub fn scan_directory_with_options(root: &Path, options: &ScanOptions) -> Result<Vec<FileInfo>> {
    let mut files = Vec::new();

    if !root.exists() {
        return Err(InstallerError::Io(std::io::Error::new(
            std::io::ErrorKind::NotFound,
            format!("Directory not found: {:?}", root),
        )));
    }

    if !root.is_dir() {
        return Err(InstallerError::Io(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            format!("Path is not a directory: {:?}", root),
        )));
    }

    let walker = WalkDir::new(root)
        .follow_links(options.follow_symlinks)
        .into_iter();

    for entry in walker.filter_map(|e| e.ok()) {
        let path = entry.path();

        // Skip directories
        if path.is_dir() {
            continue;
        }

        // Get relative path
        let relative_path = match path.strip_prefix(root) {
            Ok(p) => p.to_string_lossy().replace('\\', "/"),
            Err(e) => {
                warn!("Failed to get relative path for {:?}: {}", path, e);
                continue;
            }
        };

        // Skip hidden files if configured
        if options.skip_hidden && is_hidden_file(&relative_path) {
            debug!("Skipping hidden file: {}", relative_path);
            continue;
        }

        // Skip system files if configured (Windows-specific)
        #[cfg(windows)]
        if options.skip_system && is_system_file(path) {
            debug!("Skipping system file: {}", relative_path);
            continue;
        }

        // Check exclude patterns
        if should_exclude(&relative_path, &options.exclude_patterns) {
            debug!("Excluding file by pattern: {}", relative_path);
            continue;
        }

        // Get file metadata
        let metadata = match fs::metadata(path) {
            Ok(m) => m,
            Err(e) => {
                warn!("Failed to get metadata for {:?}: {}", path, e);
                continue;
            }
        };

        files.push(FileInfo {
            path: path.to_path_buf(),
            relative_path,
            size: metadata.len(),
            mode: get_file_mode(&metadata),
            modified: metadata.modified().unwrap_or(SystemTime::UNIX_EPOCH),
        });
    }

    // Sort by relative path for consistent ordering
    files.sort_by(|a, b| a.relative_path.cmp(&b.relative_path));

    debug!("Scanned {} files from {:?}", files.len(), root);
    Ok(files)
}

/// Check if a file is hidden (starts with '.' or is in a hidden directory).
fn is_hidden_file(relative_path: &str) -> bool {
    relative_path
        .split('/')
        .any(|component| component.starts_with('.') && component != "." && component != "..")
}

/// Check if a file is a Windows system file.
#[cfg(windows)]
fn is_system_file(path: &Path) -> bool {
    use std::os::windows::fs::MetadataExt;

    const FILE_ATTRIBUTE_SYSTEM: u32 = 0x4;
    const FILE_ATTRIBUTE_HIDDEN: u32 = 0x2;

    if let Ok(metadata) = fs::metadata(path) {
        let attrs = metadata.file_attributes();
        return (attrs & FILE_ATTRIBUTE_SYSTEM) != 0 || (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;
    }
    false
}

/// Check if a file should be excluded based on patterns.
fn should_exclude(relative_path: &str, patterns: &[String]) -> bool {
    for pattern in patterns {
        if matches_glob_pattern(relative_path, pattern) {
            return true;
        }
    }
    false
}

/// Simple glob pattern matching (supports * and **).
fn matches_glob_pattern(path: &str, pattern: &str) -> bool {
    // Simple implementation - supports basic patterns
    if pattern.contains("**") {
        // ** matches any path
        let parts: Vec<&str> = pattern.split("**").collect();
        if parts.len() == 2 {
            let prefix = parts[0].trim_end_matches('/');
            let suffix = parts[1].trim_start_matches('/');
            if !prefix.is_empty() && !path.starts_with(prefix) {
                return false;
            }
            if !suffix.is_empty() && !path.ends_with(suffix) {
                return false;
            }
            return true;
        }
    } else if pattern.contains('*') {
        // * matches any characters except /
        let parts: Vec<&str> = pattern.split('*').collect();
        if parts.len() == 2 {
            return path.starts_with(parts[0]) && path.ends_with(parts[1]);
        }
    }
    
    // Exact match - check both full path and filename only
    if path == pattern {
        return true;
    }
    
    // Also check if the filename matches (for patterns like "packager.json")
    if let Some(filename) = path.rsplit('/').next() {
        if filename == pattern {
            return true;
        }
    }
    
    false
}

/// Get file mode/permissions.
#[cfg(windows)]
fn get_file_mode(metadata: &fs::Metadata) -> u32 {
    // On Windows, use a default mode
    if metadata.permissions().readonly() {
        0o444
    } else {
        0o644
    }
}

#[cfg(not(windows))]
fn get_file_mode(metadata: &fs::Metadata) -> u32 {
    use std::os::unix::fs::PermissionsExt;
    metadata.permissions().mode()
}

// ============================================================================
// Block Division Logic
// ============================================================================

/// Result of dividing files into blocks.
#[derive(Debug, Clone)]
pub struct BlockDivisionResult {
    /// File entries with block mappings
    pub file_entries: Vec<FileEntry>,
    /// Block information (without actual data)
    pub block_infos: Vec<BlockInfo>,
    /// Raw block data for compression
    pub block_data: Vec<BlockData>,
}

/// Information about a block (before compression).
#[derive(Debug, Clone)]
pub struct BlockInfo {
    /// Block index
    pub index: u32,
    /// Original (uncompressed) size
    pub original_size: u64,
    /// Files contained in this block
    pub file_indices: Vec<usize>,
}

/// Raw data for a block.
#[derive(Debug, Clone)]
pub struct BlockData {
    /// Block index
    pub index: u32,
    /// Raw uncompressed data
    pub data: Vec<u8>,
    /// CRC32 of the raw data
    pub checksum: u32,
}

/// Divide files into blocks based on the configured block size.
///
/// # Arguments
/// * `files` - List of files to divide
/// * `block_size` - Maximum size of each block in bytes
///
/// # Returns
/// BlockDivisionResult containing file entries, block info, and block data.
///
/// # Algorithm
/// Files are packed into blocks sequentially. If a file is larger than the block size,
/// it will span multiple blocks. Small files are grouped together to fill blocks efficiently.
pub fn divide_into_blocks(files: &[FileInfo], block_size: usize) -> Result<BlockDivisionResult> {
    if block_size == 0 {
        return Err(InstallerError::Config("Block size cannot be zero".to_string()));
    }

    let mut file_entries = Vec::with_capacity(files.len());
    let mut block_infos = Vec::new();
    let mut block_data_list = Vec::new();

    let mut current_block_data = Vec::new();
    let mut current_block_files: Vec<usize> = Vec::new();
    let mut block_index = 0u32;

    for (file_idx, file) in files.iter().enumerate() {
        // Read file content
        let file_content = fs::read(&file.path)?;
        let file_checksum = crc32fast::hash(&file_content);

        let first_block_index = block_index;
        let mut blocks_for_file = 0u32;

        // Handle the file data
        let mut remaining_data = &file_content[..];

        while !remaining_data.is_empty() {
            let space_in_block = block_size.saturating_sub(current_block_data.len());

            if space_in_block == 0 {
                // Current block is full, finalize it
                let block_checksum = crc32fast::hash(&current_block_data);
                block_infos.push(BlockInfo {
                    index: block_index,
                    original_size: current_block_data.len() as u64,
                    file_indices: current_block_files.clone(),
                });
                block_data_list.push(BlockData {
                    index: block_index,
                    data: std::mem::take(&mut current_block_data),
                    checksum: block_checksum,
                });
                current_block_files.clear();
                block_index += 1;
                continue;
            }

            // Take as much data as fits in the current block
            let take_size = space_in_block.min(remaining_data.len());
            current_block_data.extend_from_slice(&remaining_data[..take_size]);
            remaining_data = &remaining_data[take_size..];

            if !current_block_files.contains(&file_idx) {
                current_block_files.push(file_idx);
            }

            // If we've consumed all data for this file, count the block
            if remaining_data.is_empty() {
                blocks_for_file = block_index - first_block_index + 1;
            }

            // If block is full after adding data, finalize it
            if current_block_data.len() >= block_size {
                let block_checksum = crc32fast::hash(&current_block_data);
                block_infos.push(BlockInfo {
                    index: block_index,
                    original_size: current_block_data.len() as u64,
                    file_indices: current_block_files.clone(),
                });
                block_data_list.push(BlockData {
                    index: block_index,
                    data: std::mem::take(&mut current_block_data),
                    checksum: block_checksum,
                });
                current_block_files.clear();
                block_index += 1;
            }
        }

        // Create file entry
        file_entries.push(FileEntry {
            path: file.relative_path.clone(),
            original_size: file.size,
            mode: file.mode,
            first_block_index,
            block_count: blocks_for_file.max(1),
            checksum: file_checksum,
        });
    }

    // Don't forget the last block if it has data
    if !current_block_data.is_empty() {
        let block_checksum = crc32fast::hash(&current_block_data);
        block_infos.push(BlockInfo {
            index: block_index,
            original_size: current_block_data.len() as u64,
            file_indices: current_block_files,
        });
        block_data_list.push(BlockData {
            index: block_index,
            data: current_block_data,
            checksum: block_checksum,
        });
    }

    debug!(
        "Divided {} files into {} blocks (block_size={})",
        files.len(),
        block_data_list.len(),
        block_size
    );

    Ok(BlockDivisionResult {
        file_entries,
        block_infos,
        block_data: block_data_list,
    })
}

/// Calculate the expected number of blocks for a given total size and block size.
///
/// This is useful for progress reporting and validation.
pub fn calculate_block_count(total_size: u64, block_size: usize) -> u32 {
    if block_size == 0 || total_size == 0 {
        return 0;
    }
    let block_size = block_size as u64;
    ((total_size + block_size - 1) / block_size) as u32
}

// ============================================================================
// File Writing Operations
// ============================================================================

/// Create a directory and all parent directories.
pub fn create_dir_all(path: &Path) -> Result<()> {
    fs::create_dir_all(path)?;
    Ok(())
}

/// Write data to a file, creating parent directories if needed.
///
/// # Arguments
/// * `path` - Target file path
/// * `data` - Data to write
///
/// # Returns
/// Ok(()) on success, or an error if writing fails.
pub fn write_file(path: &Path, data: &[u8]) -> Result<()> {
    if let Some(parent) = path.parent() {
        create_dir_all(parent)?;
    }
    fs::write(path, data)?;
    debug!("Wrote {} bytes to {:?}", data.len(), path);
    Ok(())
}

/// Write data to a file with specific permissions.
///
/// # Arguments
/// * `path` - Target file path
/// * `data` - Data to write
/// * `mode` - File permissions (Unix mode)
///
/// # Returns
/// Ok(()) on success, or an error if writing fails.
pub fn write_file_with_mode(path: &Path, data: &[u8], mode: u32) -> Result<()> {
    if let Some(parent) = path.parent() {
        create_dir_all(parent)?;
    }

    // Write the file
    let mut file = fs::File::create(path)?;
    file.write_all(data)?;
    file.flush()?;

    // Set permissions
    set_file_permissions(path, mode)?;

    debug!("Wrote {} bytes to {:?} with mode {:o}", data.len(), path, mode);
    Ok(())
}

/// Set file permissions (public wrapper).
pub fn set_file_permissions_public(path: &Path, mode: u32) -> Result<()> {
    set_file_permissions(path, mode)
}

/// Set file permissions.
#[cfg(windows)]
fn set_file_permissions(path: &Path, mode: u32) -> Result<()> {
    // On Windows, we can only set read-only
    let readonly = (mode & 0o200) == 0; // No write permission
    let mut perms = fs::metadata(path)?.permissions();
    perms.set_readonly(readonly);
    fs::set_permissions(path, perms)?;
    Ok(())
}

#[cfg(not(windows))]
fn set_file_permissions(path: &Path, mode: u32) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    let perms = Permissions::from_mode(mode);
    fs::set_permissions(path, perms)?;
    Ok(())
}

/// Delete a file.
pub fn delete_file(path: &Path) -> Result<()> {
    if path.exists() {
        fs::remove_file(path)?;
        debug!("Deleted file: {:?}", path);
    }
    Ok(())
}

/// Delete a directory and all its contents.
pub fn delete_dir_all(path: &Path) -> Result<()> {
    if path.exists() {
        fs::remove_dir_all(path)?;
        debug!("Deleted directory: {:?}", path);
    }
    Ok(())
}

/// Delete an empty directory.
pub fn delete_empty_dir(path: &Path) -> Result<()> {
    if path.exists() && path.is_dir() {
        // Check if directory is empty
        if fs::read_dir(path)?.next().is_none() {
            fs::remove_dir(path)?;
            debug!("Deleted empty directory: {:?}", path);
        }
    }
    Ok(())
}

// ============================================================================
// Disk Space Operations
// ============================================================================

/// Get available disk space on the drive containing the given path.
pub fn get_available_space(path: &Path) -> Result<u64> {
    use fs2::available_space;

    // Find an existing ancestor path
    let mut check_path = path.to_path_buf();
    while !check_path.exists() {
        if let Some(parent) = check_path.parent() {
            check_path = parent.to_path_buf();
        } else {
            return Err(InstallerError::Io(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                "Cannot find existing path to check disk space",
            )));
        }
    }

    available_space(&check_path).map_err(InstallerError::Io)
}

/// Check if there is sufficient disk space.
///
/// # Arguments
/// * `path` - Target installation path
/// * `required` - Required space in bytes
/// * `buffer` - Additional buffer space (default: 100MB)
///
/// # Returns
/// Ok(()) if sufficient space, or InsufficientDiskSpace error.
pub fn check_disk_space(path: &Path, required: u64, buffer: u64) -> Result<()> {
    let available = get_available_space(path)?;
    let total_required = required.saturating_add(buffer);

    debug!(
        "Disk space check: required={} + buffer={} = {}, available={}",
        required, buffer, total_required, available
    );

    if available < total_required {
        return Err(InstallerError::InsufficientDiskSpace {
            required: total_required,
            available,
        });
    }

    Ok(())
}

/// Check disk space with default buffer (100MB).
pub fn check_disk_space_with_default_buffer(path: &Path, required: u64) -> Result<()> {
    check_disk_space(path, required, DEFAULT_DISK_SPACE_BUFFER)
}

/// Calculate required space considering compression ratio.
///
/// # Arguments
/// * `uncompressed_size` - Total uncompressed size
/// * `_compression_ratio` - Expected compression ratio (0.0 to 1.0, where 0.5 means 50% of original)
///
/// # Returns
/// Estimated required space in bytes.
pub fn calculate_required_space(uncompressed_size: u64, _compression_ratio: f64) -> u64 {
    // For installation, we need the uncompressed size
    // The compression_ratio is informational for the package size
    uncompressed_size
}

/// Get disk space information for display.
#[derive(Debug, Clone)]
pub struct DiskSpaceInfo {
    /// Available space in bytes
    pub available: u64,
    /// Required space in bytes
    pub required: u64,
    /// Buffer space in bytes
    pub buffer: u64,
    /// Whether there is sufficient space
    pub sufficient: bool,
}

/// Get detailed disk space information.
pub fn get_disk_space_info(path: &Path, required: u64, buffer: u64) -> Result<DiskSpaceInfo> {
    let available = get_available_space(path)?;
    let total_required = required.saturating_add(buffer);

    Ok(DiskSpaceInfo {
        available,
        required,
        buffer,
        sufficient: available >= total_required,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_scan_directory() {
        let dir = tempdir().unwrap();
        let file1 = dir.path().join("file1.txt");
        let subdir = dir.path().join("subdir");
        let file2 = subdir.join("file2.txt");

        fs::write(&file1, b"content1").unwrap();
        fs::create_dir(&subdir).unwrap();
        fs::write(&file2, b"content2").unwrap();

        let files = scan_directory(dir.path()).unwrap();
        assert_eq!(files.len(), 2);

        let paths: Vec<_> = files.iter().map(|f| f.relative_path.as_str()).collect();
        assert!(paths.contains(&"file1.txt"));
        assert!(paths.contains(&"subdir/file2.txt"));
    }

    #[test]
    fn test_scan_directory_with_hidden_filter() {
        let dir = tempdir().unwrap();
        let visible = dir.path().join("visible.txt");
        let hidden = dir.path().join(".hidden.txt");
        let hidden_dir = dir.path().join(".hidden_dir");
        let file_in_hidden = hidden_dir.join("file.txt");

        fs::write(&visible, b"visible").unwrap();
        fs::write(&hidden, b"hidden").unwrap();
        fs::create_dir(&hidden_dir).unwrap();
        fs::write(&file_in_hidden, b"in hidden").unwrap();

        // Without filter
        let files = scan_directory(dir.path()).unwrap();
        assert_eq!(files.len(), 3);

        // With hidden filter
        let options = ScanOptions {
            skip_hidden: true,
            ..Default::default()
        };
        let files = scan_directory_with_options(dir.path(), &options).unwrap();
        assert_eq!(files.len(), 1);
        assert_eq!(files[0].relative_path, "visible.txt");
    }

    #[test]
    fn test_scan_directory_with_exclude_patterns() {
        let dir = tempdir().unwrap();
        let file1 = dir.path().join("file.txt");
        let file2 = dir.path().join("file.log");
        let file3 = dir.path().join("data.txt");

        fs::write(&file1, b"txt").unwrap();
        fs::write(&file2, b"log").unwrap();
        fs::write(&file3, b"data").unwrap();

        let options = ScanOptions {
            exclude_patterns: vec!["*.log".to_string()],
            ..Default::default()
        };
        let files = scan_directory_with_options(dir.path(), &options).unwrap();
        assert_eq!(files.len(), 2);

        let paths: Vec<_> = files.iter().map(|f| f.relative_path.as_str()).collect();
        assert!(paths.contains(&"file.txt"));
        assert!(paths.contains(&"data.txt"));
        assert!(!paths.contains(&"file.log"));
    }

    #[test]
    fn test_scan_nonexistent_directory() {
        let result = scan_directory(Path::new("/nonexistent/path/12345"));
        assert!(result.is_err());
    }

    #[test]
    fn test_write_and_delete_file() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("nested/dir/file.txt");

        write_file(&file, b"test content").unwrap();
        assert!(file.exists());
        assert_eq!(fs::read(&file).unwrap(), b"test content");

        delete_file(&file).unwrap();
        assert!(!file.exists());
    }

    #[test]
    fn test_write_file_with_mode() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("file.txt");

        write_file_with_mode(&file, b"content", 0o644).unwrap();
        assert!(file.exists());

        #[cfg(not(windows))]
        {
            use std::os::unix::fs::PermissionsExt;
            let perms = fs::metadata(&file).unwrap().permissions();
            assert_eq!(perms.mode() & 0o777, 0o644);
        }
    }

    #[test]
    fn test_divide_into_blocks_single_file() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("file.txt");
        let content = b"Hello, World!";
        fs::write(&file, content).unwrap();

        let files = scan_directory(dir.path()).unwrap();
        let result = divide_into_blocks(&files, 1024).unwrap();

        assert_eq!(result.file_entries.len(), 1);
        assert_eq!(result.block_data.len(), 1);
        assert_eq!(result.file_entries[0].original_size, content.len() as u64);
        assert_eq!(result.file_entries[0].block_count, 1);
    }

    #[test]
    fn test_divide_into_blocks_multiple_files() {
        let dir = tempdir().unwrap();
        let file1 = dir.path().join("file1.txt");
        let file2 = dir.path().join("file2.txt");
        fs::write(&file1, b"content1").unwrap();
        fs::write(&file2, b"content2").unwrap();

        let files = scan_directory(dir.path()).unwrap();
        let result = divide_into_blocks(&files, 1024).unwrap();

        assert_eq!(result.file_entries.len(), 2);
        // Both files fit in one block
        assert_eq!(result.block_data.len(), 1);
    }

    #[test]
    fn test_divide_into_blocks_large_file() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("large.bin");
        let content = vec![0u8; 1000]; // 1000 bytes
        fs::write(&file, &content).unwrap();

        let files = scan_directory(dir.path()).unwrap();
        // Use small block size to force multiple blocks
        let result = divide_into_blocks(&files, 300).unwrap();

        assert_eq!(result.file_entries.len(), 1);
        // 1000 bytes / 300 = 4 blocks (ceil)
        assert_eq!(result.block_data.len(), 4);
        assert!(result.file_entries[0].block_count >= 4);
    }

    #[test]
    fn test_divide_into_blocks_zero_block_size() {
        let dir = tempdir().unwrap();
        let file = dir.path().join("file.txt");
        fs::write(&file, b"content").unwrap();

        let files = scan_directory(dir.path()).unwrap();
        let result = divide_into_blocks(&files, 0);

        assert!(result.is_err());
    }

    #[test]
    fn test_calculate_block_count() {
        assert_eq!(calculate_block_count(0, 1024), 0);
        assert_eq!(calculate_block_count(1024, 1024), 1);
        assert_eq!(calculate_block_count(1025, 1024), 2);
        assert_eq!(calculate_block_count(2048, 1024), 2);
        assert_eq!(calculate_block_count(100, 0), 0);
    }

    #[test]
    fn test_disk_space_check() {
        let dir = tempdir().unwrap();

        // Should succeed with small requirement
        let result = check_disk_space(dir.path(), 1024, 1024);
        assert!(result.is_ok());

        // Should fail with huge requirement
        let result = check_disk_space(dir.path(), u64::MAX / 2, 0);
        assert!(result.is_err());
    }

    #[test]
    fn test_get_disk_space_info() {
        let dir = tempdir().unwrap();
        let info = get_disk_space_info(dir.path(), 1024, 100).unwrap();

        assert!(info.available > 0);
        assert_eq!(info.required, 1024);
        assert_eq!(info.buffer, 100);
        assert!(info.sufficient);
    }

    #[test]
    fn test_is_hidden_file() {
        assert!(is_hidden_file(".hidden"));
        assert!(is_hidden_file(".hidden/file.txt"));
        assert!(is_hidden_file("dir/.hidden/file.txt"));
        assert!(!is_hidden_file("visible.txt"));
        assert!(!is_hidden_file("dir/visible.txt"));
    }

    #[test]
    fn test_matches_glob_pattern() {
        assert!(matches_glob_pattern("file.log", "*.log"));
        assert!(matches_glob_pattern("test.log", "*.log"));
        assert!(!matches_glob_pattern("file.txt", "*.log"));

        assert!(matches_glob_pattern("dir/file.txt", "**/file.txt"));
        assert!(matches_glob_pattern("a/b/c/file.txt", "**/file.txt"));
    }
}


// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use proptest::prelude::*;
    use tempfile::tempdir;

    // Strategy to generate random file names (valid for filesystem)
    fn valid_filename() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9_]{1,20}\\.(txt|bin|dat|log)"
    }

    // Strategy to generate random file content
    fn file_content() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 0..10000)
    }

    // Strategy to generate a list of files to create
    fn file_list() -> impl Strategy<Value = Vec<(String, Vec<u8>)>> {
        prop::collection::vec((valid_filename(), file_content()), 1..20)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// **Property 11: File Scan Completeness**
        /// For any directory structure, recursive scanning should discover all regular files
        /// without missing any file.
        ///
        /// **Validates: Requirements 2.1**
        #[test]
        fn prop_file_scan_completeness(files in file_list()) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create all files
            let mut expected_paths: Vec<String> = Vec::new();
            for (name, content) in &files {
                // Ensure unique names by adding index
                let unique_name = format!("{}_{}", expected_paths.len(), name);
                let file_path = dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                expected_paths.push(unique_name);
            }

            // Scan directory
            let scanned = scan_directory(dir.path()).expect("Failed to scan directory");

            // Property: All created files should be found
            prop_assert_eq!(
                scanned.len(),
                expected_paths.len(),
                "Scanned file count should match created file count"
            );

            // Property: Each scanned file should have correct size
            for file_info in &scanned {
                let original_idx = expected_paths.iter()
                    .position(|p| p == &file_info.relative_path)
                    .expect("Scanned file not in expected list");
                let expected_size = files[original_idx].1.len() as u64;
                prop_assert_eq!(
                    file_info.size,
                    expected_size,
                    "File size should match for {}",
                    file_info.relative_path
                );
            }
        }

        /// **Property 11 (continued): File Scan with Subdirectories**
        /// Scanning should work correctly with nested directory structures.
        ///
        /// **Validates: Requirements 2.1**
        #[test]
        fn prop_file_scan_with_subdirs(
            root_files in prop::collection::vec((valid_filename(), file_content()), 0..5),
            subdir_files in prop::collection::vec((valid_filename(), file_content()), 0..5)
        ) {
            let dir = tempdir().expect("Failed to create temp dir");
            let subdir = dir.path().join("subdir");
            fs::create_dir(&subdir).expect("Failed to create subdir");

            let mut total_files = 0;

            // Create root files
            for (i, (name, content)) in root_files.iter().enumerate() {
                let unique_name = format!("root_{}_{}", i, name);
                let file_path = dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write root file");
                total_files += 1;
            }

            // Create subdir files
            for (i, (name, content)) in subdir_files.iter().enumerate() {
                let unique_name = format!("sub_{}_{}", i, name);
                let file_path = subdir.join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write subdir file");
                total_files += 1;
            }

            // Scan directory
            let scanned = scan_directory(dir.path()).expect("Failed to scan directory");

            // Property: All files should be found
            prop_assert_eq!(
                scanned.len(),
                total_files,
                "Should find all files in root and subdirectories"
            );

            // Property: Subdir files should have correct relative paths
            for file_info in &scanned {
                if file_info.relative_path.starts_with("subdir/") {
                    prop_assert!(
                        file_info.relative_path.contains('/'),
                        "Subdir files should have path separator"
                    );
                }
            }
        }

        /// **Property 11 (continued): File Scan Ordering**
        /// Scanned files should be sorted by relative path for consistent ordering.
        ///
        /// **Validates: Requirements 2.1**
        #[test]
        fn prop_file_scan_ordering(files in file_list()) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create files
            for (i, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("file_{}_{}", i, name);
                let file_path = dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
            }

            // Scan directory
            let scanned = scan_directory(dir.path()).expect("Failed to scan directory");

            // Property: Files should be sorted by relative path
            let paths: Vec<&str> = scanned.iter().map(|f| f.relative_path.as_str()).collect();
            let mut sorted_paths = paths.clone();
            sorted_paths.sort();

            prop_assert_eq!(
                paths,
                sorted_paths,
                "Scanned files should be sorted by relative path"
            );
        }
    }
}

#[cfg(test)]
mod block_division_property_tests {
    use super::*;
    use proptest::prelude::*;
    use tempfile::tempdir;

    // Strategy for block sizes (reasonable range)
    fn block_size_strategy() -> impl Strategy<Value = usize> {
        100usize..10000
    }

    // Strategy for file content
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 1..5000)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// **Property 3: Block Division Consistency**
        /// For any file and block size configuration, the number of blocks generated
        /// should equal ceil(file_size / block_size), and the last block's size
        /// should be less than or equal to block_size.
        ///
        /// **Validates: Requirements 2.3**
        #[test]
        fn prop_block_division_consistency(
            content in file_content_strategy(),
            block_size in block_size_strategy()
        ) {
            let dir = tempdir().expect("Failed to create temp dir");
            let file_path = dir.path().join("test_file.bin");
            fs::write(&file_path, &content).expect("Failed to write file");

            let files = scan_directory(dir.path()).expect("Failed to scan");
            let result = divide_into_blocks(&files, block_size).expect("Failed to divide");

            let file_size = content.len();
            let expected_blocks = if file_size == 0 {
                0
            } else {
                (file_size + block_size - 1) / block_size
            };

            // Property: Number of blocks should match expected
            prop_assert_eq!(
                result.block_data.len(),
                expected_blocks,
                "Block count should be ceil(file_size / block_size)"
            );

            // Property: All blocks except possibly the last should be full
            for (i, block) in result.block_data.iter().enumerate() {
                if i < result.block_data.len() - 1 {
                    prop_assert_eq!(
                        block.data.len(),
                        block_size,
                        "Non-last blocks should be exactly block_size"
                    );
                } else {
                    prop_assert!(
                        block.data.len() <= block_size,
                        "Last block should be <= block_size"
                    );
                    prop_assert!(
                        block.data.len() > 0,
                        "Last block should not be empty"
                    );
                }
            }

            // Property: Total data size should equal original file size
            let total_block_data: usize = result.block_data.iter().map(|b| b.data.len()).sum();
            prop_assert_eq!(
                total_block_data,
                file_size,
                "Total block data should equal original file size"
            );
        }

        /// **Property 3 (continued): Block Division with Multiple Files**
        /// Block division should correctly handle multiple files packed into blocks.
        ///
        /// **Validates: Requirements 2.3**
        #[test]
        fn prop_block_division_multiple_files(
            file_contents in prop::collection::vec(file_content_strategy(), 1..10),
            block_size in block_size_strategy()
        ) {
            let dir = tempdir().expect("Failed to create temp dir");

            // Create multiple files
            let mut total_size = 0usize;
            for (i, content) in file_contents.iter().enumerate() {
                let file_path = dir.path().join(format!("file_{}.bin", i));
                fs::write(&file_path, content).expect("Failed to write file");
                total_size += content.len();
            }

            let files = scan_directory(dir.path()).expect("Failed to scan");
            let result = divide_into_blocks(&files, block_size).expect("Failed to divide");

            // Property: Number of file entries should match input files
            prop_assert_eq!(
                result.file_entries.len(),
                file_contents.len(),
                "File entry count should match input file count"
            );

            // Property: Total block data should equal total file sizes
            let total_block_data: usize = result.block_data.iter().map(|b| b.data.len()).sum();
            prop_assert_eq!(
                total_block_data,
                total_size,
                "Total block data should equal sum of all file sizes"
            );

            // Property: Each file entry should have valid block references
            for entry in &result.file_entries {
                prop_assert!(
                    entry.first_block_index < result.block_data.len() as u32,
                    "First block index should be valid"
                );
                prop_assert!(
                    entry.block_count > 0,
                    "Block count should be at least 1"
                );
            }
        }

        /// **Property 3 (continued): Block Checksum Validity**
        /// Each block should have a valid CRC32 checksum that matches its data.
        ///
        /// **Validates: Requirements 2.3**
        #[test]
        fn prop_block_checksum_validity(
            content in file_content_strategy(),
            block_size in block_size_strategy()
        ) {
            let dir = tempdir().expect("Failed to create temp dir");
            let file_path = dir.path().join("test_file.bin");
            fs::write(&file_path, &content).expect("Failed to write file");

            let files = scan_directory(dir.path()).expect("Failed to scan");
            let result = divide_into_blocks(&files, block_size).expect("Failed to divide");

            // Property: Each block's checksum should match its data
            for block in &result.block_data {
                let computed_checksum = crc32fast::hash(&block.data);
                prop_assert_eq!(
                    block.checksum,
                    computed_checksum,
                    "Block checksum should match computed CRC32"
                );
            }
        }
    }
}
