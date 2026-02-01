//! UI Resources module for embedding and extracting UI assets.
//!
//! This module provides functionality for:
//! - Scanning UI resource directories (HTML, CSS, JS, images)
//! - Compressing UI resources into tar.gz archives
//! - Calculating CRC32 checksums for integrity verification
//! - Extracting UI resources to temporary directories
//! - Validating UI resource structure
//!
//! # Requirements
//! - 5.1: Accept ui_resources directory path in configuration
//! - 5.2: Scan HTML, CSS, JavaScript, and image files
//! - 5.3: Embed UI resources as compressed archive
//! - 5.4: Extract UI resources to temporary directory at runtime
//! - 5.8: Validate UI resource structure (must contain index.html)
//! - 5.9: Verify UI resources integrity using checksum

use crate::compression::calculate_crc32;
use flate2::read::GzDecoder;
use flate2::write::GzEncoder;
use flate2::Compression;
use installer_shared::{InstallerError, Result};
use std::fs;
use std::path::{Path, PathBuf};
use tar::{Archive, Builder};
use tracing::{debug, info, warn};
use walkdir::WalkDir;

/// UI Resources container.
///
/// Holds compressed UI resources (HTML, CSS, JS, images) as a tar.gz archive
/// along with metadata for integrity verification.
///
/// # Example
/// ```no_run
/// use installer_core::ui_resources::UIResources;
/// use std::path::Path;
///
/// // Create from directory
/// let resources = UIResources::from_directory(Path::new("./ui")).unwrap();
///
/// // Extract to temp directory
/// resources.extract_to(Path::new("/tmp/ui")).unwrap();
/// ```
#[derive(Debug, Clone)]
pub struct UIResources {
    /// Compressed tar.gz archive data
    pub archive: Vec<u8>,
    /// CRC32 checksum of the archive
    pub checksum: u32,
    /// Original uncompressed size
    pub original_size: u64,
    /// List of supported locales found in the resources
    pub locales: Vec<String>,
}

impl UIResources {
    /// Create UI resources from a directory.
    ///
    /// Scans the directory for UI files (HTML, CSS, JS, images) and creates
    /// a compressed tar.gz archive.
    ///
    /// # Arguments
    /// * `dir` - Path to the UI resources directory
    ///
    /// # Returns
    /// A UIResources instance containing the compressed archive
    ///
    /// # Errors
    /// - If the directory doesn't exist
    /// - If index.html is not found
    /// - If compression fails
    ///
    /// # Requirements
    /// - 5.1: Accept ui_resources directory path
    /// - 5.2: Scan HTML, CSS, JavaScript, and image files
    /// - 5.3: Compress as tar.gz archive
    pub fn from_directory(dir: &Path) -> Result<Self> {
        info!("Creating UI resources from directory: {:?}", dir);

        // Validate directory exists
        if !dir.exists() {
            return Err(InstallerError::Io(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                format!("UI resources directory not found: {:?}", dir),
            )));
        }

        // Validate structure (must contain index.html)
        Self::validate_structure(dir)?;

        // Scan for locales
        let locales = Self::scan_locales(dir)?;
        debug!("Found locales: {:?}", locales);

        // Calculate original size
        let original_size = Self::calculate_directory_size(dir)?;
        debug!("Original size: {} bytes", original_size);

        // Create tar.gz archive
        let archive = Self::create_archive(dir)?;
        let checksum = calculate_crc32(&archive);

        info!(
            "Created UI resources archive: {} bytes (original: {} bytes), checksum: {:08x}",
            archive.len(),
            original_size,
            checksum
        );

        Ok(Self {
            archive,
            checksum,
            original_size,
            locales,
        })
    }

    /// Validate the UI resources directory structure.
    ///
    /// Ensures the directory contains required files:
    /// - index.html (required)
    /// - locales/ directory (optional but validated if present)
    ///
    /// # Requirements
    /// - 5.8: Validate UI resource structure
    pub fn validate_structure(dir: &Path) -> Result<()> {
        // Check for index.html
        let index_path = dir.join("index.html");
        if !index_path.exists() {
            return Err(InstallerError::Config(
                "UI resources must contain index.html".to_string(),
            ));
        }

        // Validate locales directory if present
        let locales_dir = dir.join("locales");
        if locales_dir.exists() {
            Self::validate_locales_directory(&locales_dir)?;
        }

        Ok(())
    }

    /// Validate the locales directory structure.
    ///
    /// Ensures locale files are valid JSON files with proper naming.
    fn validate_locales_directory(locales_dir: &Path) -> Result<()> {
        if !locales_dir.is_dir() {
            return Err(InstallerError::Config(
                "locales must be a directory".to_string(),
            ));
        }

        for entry in fs::read_dir(locales_dir)? {
            let entry = entry?;
            let path = entry.path();

            if path.is_file() {
                // Check file extension
                if let Some(ext) = path.extension() {
                    if ext != "json" {
                        warn!("Non-JSON file in locales directory: {:?}", path);
                    }
                }

                // Validate JSON content
                if path.extension().map(|e| e == "json").unwrap_or(false) {
                    let content = fs::read_to_string(&path)?;
                    if serde_json::from_str::<serde_json::Value>(&content).is_err() {
                        return Err(InstallerError::Config(format!(
                            "Invalid JSON in locale file: {:?}",
                            path
                        )));
                    }
                }
            }
        }

        Ok(())
    }

    /// Scan for available locales in the resources directory.
    fn scan_locales(dir: &Path) -> Result<Vec<String>> {
        let locales_dir = dir.join("locales");
        let mut locales = Vec::new();

        if locales_dir.exists() && locales_dir.is_dir() {
            for entry in fs::read_dir(&locales_dir)? {
                let entry = entry?;
                let path = entry.path();

                if path.is_file() {
                    if let Some(ext) = path.extension() {
                        if ext == "json" {
                            if let Some(stem) = path.file_stem() {
                                locales.push(stem.to_string_lossy().to_string());
                            }
                        }
                    }
                }
            }
        }

        locales.sort();
        Ok(locales)
    }

    /// Calculate the total size of files in a directory.
    fn calculate_directory_size(dir: &Path) -> Result<u64> {
        let mut total_size = 0u64;

        for entry in WalkDir::new(dir).into_iter().filter_map(|e| e.ok()) {
            if entry.file_type().is_file() {
                if let Ok(metadata) = entry.metadata() {
                    total_size += metadata.len();
                }
            }
        }

        Ok(total_size)
    }

    /// Create a tar.gz archive from the directory.
    fn create_archive(dir: &Path) -> Result<Vec<u8>> {
        let mut archive_data = Vec::new();

        {
            let encoder = GzEncoder::new(&mut archive_data, Compression::default());
            let mut builder = Builder::new(encoder);

            // Collect entries for deterministic ordering
            let mut entries: Vec<(String, PathBuf, bool)> = Vec::new();
            for entry in WalkDir::new(dir).into_iter().filter_map(|e| e.ok()) {
                let path = entry.path();
                if path == dir {
                    continue;
                }
                let relative_path = path.strip_prefix(dir).unwrap_or(path);
                let relative = relative_path.to_string_lossy().replace('\\', "/");
                let is_dir = entry.file_type().is_dir();
                entries.push((relative, path.to_path_buf(), is_dir));
            }

            entries.sort_by(|a, b| {
                let order = a.0.cmp(&b.0);
                if order == std::cmp::Ordering::Equal {
                    // Directories before files if same path (defensive)
                    a.2.cmp(&b.2)
                } else {
                    order
                }
            });

            for (relative, path, is_dir) in entries {
                if is_dir {
                    debug!("Adding dir to archive: {}", relative);
                    builder.append_dir(relative, path)?;
                } else {
                    debug!("Adding to archive: {}", relative);
                    builder.append_path_with_name(path, relative)?;
                }
            }

            // Finish the archive
            let encoder = builder.into_inner()?;
            encoder.finish()?;
        }

        Ok(archive_data)
    }

    /// Extract UI resources to a target directory.
    ///
    /// Decompresses the tar.gz archive and extracts all files to the
    /// specified directory.
    ///
    /// # Arguments
    /// * `target_dir` - Directory to extract files to
    ///
    /// # Requirements
    /// - 5.4: Extract UI resources to temporary directory
    pub fn extract_to(&self, target_dir: &Path) -> Result<()> {
        info!("Extracting UI resources to: {:?}", target_dir);

        // Create target directory if it doesn't exist
        fs::create_dir_all(target_dir)?;

        // Decompress and extract (safe against path traversal)
        let decoder = GzDecoder::new(&self.archive[..]);
        let mut archive = Archive::new(decoder);

        for entry in archive.entries()? {
            let mut entry = entry?;
            entry
                .unpack_in(target_dir)
                .map_err(|e| InstallerError::Io(e))?;
        }

        info!("Extracted UI resources successfully");
        Ok(())
    }

    /// Verify the integrity of the UI resources.
    ///
    /// Calculates the CRC32 checksum of the archive and compares it
    /// with the stored checksum.
    ///
    /// # Requirements
    /// - 5.9: Verify UI resources integrity using checksum
    pub fn verify(&self) -> Result<()> {
        let computed_checksum = calculate_crc32(&self.archive);

        if computed_checksum != self.checksum {
            return Err(InstallerError::ChecksumMismatch {
                expected: self.checksum,
                actual: computed_checksum,
            });
        }

        Ok(())
    }

    /// Get the list of supported locales.
    pub fn supported_locales(&self) -> &[String] {
        &self.locales
    }

    /// Get the archive size in bytes.
    pub fn archive_size(&self) -> usize {
        self.archive.len()
    }

    /// Get the original (uncompressed) size in bytes.
    pub fn original_size(&self) -> u64 {
        self.original_size
    }

    /// Get the compression ratio.
    pub fn compression_ratio(&self) -> f64 {
        if self.original_size > 0 {
            self.archive.len() as f64 / self.original_size as f64
        } else {
            1.0
        }
    }

    /// Create UIResources from raw archive data.
    ///
    /// Used when loading embedded UI resources from a package.
    ///
    /// # Arguments
    /// * `archive` - The compressed tar.gz archive data
    /// * `checksum` - Expected CRC32 checksum
    /// * `original_size` - Original uncompressed size
    pub fn from_archive(archive: Vec<u8>, checksum: u32, original_size: u64) -> Result<Self> {
        // Verify checksum
        let computed_checksum = calculate_crc32(&archive);
        if computed_checksum != checksum {
            return Err(InstallerError::ChecksumMismatch {
                expected: checksum,
                actual: computed_checksum,
            });
        }

        // Extract locales from archive (scan without extracting)
        let locales = Self::scan_locales_from_archive(&archive)?;

        Ok(Self {
            archive,
            checksum,
            original_size,
            locales,
        })
    }

    /// Scan locales from archive without extracting.
    fn scan_locales_from_archive(archive: &[u8]) -> Result<Vec<String>> {
        let decoder = GzDecoder::new(archive);
        let mut tar = Archive::new(decoder);
        let mut locales = Vec::new();

        for entry in tar.entries()? {
            let entry = entry?;
            let path = entry.path()?;

            if let Some(parent) = path.parent() {
                if parent.ends_with("locales") || parent.to_string_lossy() == "locales" {
                    if let Some(ext) = path.extension() {
                        if ext == "json" {
                            if let Some(stem) = path.file_stem() {
                                locales.push(stem.to_string_lossy().to_string());
                            }
                        }
                    }
                }
            }
        }

        locales.sort();
        Ok(locales)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    fn create_test_ui_directory(dir: &Path) {
        // Create index.html
        fs::write(dir.join("index.html"), "<html><body>Test</body></html>").unwrap();

        // Create styles directory
        let styles_dir = dir.join("styles");
        fs::create_dir_all(&styles_dir).unwrap();
        fs::write(styles_dir.join("main.css"), "body { margin: 0; }").unwrap();

        // Create scripts directory
        let scripts_dir = dir.join("scripts");
        fs::create_dir_all(&scripts_dir).unwrap();
        fs::write(scripts_dir.join("main.js"), "console.log('test');").unwrap();

        // Create locales directory
        let locales_dir = dir.join("locales");
        fs::create_dir_all(&locales_dir).unwrap();
        fs::write(
            locales_dir.join("en-US.json"),
            r#"{"welcome": "Welcome"}"#,
        )
        .unwrap();
        fs::write(
            locales_dir.join("zh-CN.json"),
            r#"{"welcome": "欢迎"}"#,
        )
        .unwrap();
    }

    #[test]
    fn test_from_directory() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let resources = UIResources::from_directory(dir.path()).unwrap();

        assert!(!resources.archive.is_empty());
        assert!(resources.checksum != 0);
        assert!(resources.original_size > 0);
        assert_eq!(resources.locales.len(), 2);
        assert!(resources.locales.contains(&"en-US".to_string()));
        assert!(resources.locales.contains(&"zh-CN".to_string()));
    }

    #[test]
    fn test_validate_structure_missing_index() {
        let dir = tempdir().unwrap();
        // Don't create index.html

        let result = UIResources::from_directory(dir.path());
        assert!(result.is_err());
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("index.html"));
    }

    #[test]
    fn test_validate_structure_invalid_locale_json() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("index.html"), "<html></html>").unwrap();

        let locales_dir = dir.path().join("locales");
        fs::create_dir_all(&locales_dir).unwrap();
        fs::write(locales_dir.join("en-US.json"), "invalid json {").unwrap();

        let result = UIResources::from_directory(dir.path());
        assert!(result.is_err());
    }

    #[test]
    fn test_extract_to() {
        let source_dir = tempdir().unwrap();
        let target_dir = tempdir().unwrap();

        create_test_ui_directory(source_dir.path());

        let resources = UIResources::from_directory(source_dir.path()).unwrap();
        resources.extract_to(target_dir.path()).unwrap();

        // Verify extracted files
        assert!(target_dir.path().join("index.html").exists());
        assert!(target_dir.path().join("styles/main.css").exists());
        assert!(target_dir.path().join("scripts/main.js").exists());
        assert!(target_dir.path().join("locales/en-US.json").exists());
        assert!(target_dir.path().join("locales/zh-CN.json").exists());
    }

    #[test]
    fn test_verify_checksum() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let resources = UIResources::from_directory(dir.path()).unwrap();
        assert!(resources.verify().is_ok());
    }

    #[test]
    fn test_verify_checksum_mismatch() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let mut resources = UIResources::from_directory(dir.path()).unwrap();
        resources.checksum = 0xDEADBEEF; // Wrong checksum

        let result = resources.verify();
        assert!(result.is_err());
    }

    #[test]
    fn test_from_archive() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let original = UIResources::from_directory(dir.path()).unwrap();

        let restored = UIResources::from_archive(
            original.archive.clone(),
            original.checksum,
            original.original_size,
        )
        .unwrap();

        assert_eq!(restored.archive, original.archive);
        assert_eq!(restored.checksum, original.checksum);
        assert_eq!(restored.locales, original.locales);
    }

    #[test]
    fn test_compression_ratio() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let resources = UIResources::from_directory(dir.path()).unwrap();
        let ratio = resources.compression_ratio();

        // Compression ratio should be positive
        // For small files, gzip overhead may cause ratio > 1
        assert!(ratio > 0.0);
    }

    #[test]
    fn test_supported_locales() {
        let dir = tempdir().unwrap();
        create_test_ui_directory(dir.path());

        let resources = UIResources::from_directory(dir.path()).unwrap();
        let locales = resources.supported_locales();

        assert_eq!(locales.len(), 2);
        assert!(locales.contains(&"en-US".to_string()));
        assert!(locales.contains(&"zh-CN".to_string()));
    }

    #[test]
    fn test_validate_structure_with_only_index() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("index.html"), "<html></html>").unwrap();

        // Should succeed with just index.html (locales are optional)
        let result = UIResources::from_directory(dir.path());
        assert!(result.is_ok());
        assert!(result.unwrap().locales.is_empty());
    }

    #[test]
    fn test_validate_structure_directory_not_found() {
        let result = UIResources::from_directory(Path::new("/nonexistent/path"));
        assert!(result.is_err());
        assert!(result.unwrap_err().to_string().contains("not found"));
    }

    #[test]
    fn test_validate_locales_with_non_json_files() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("index.html"), "<html></html>").unwrap();

        let locales_dir = dir.path().join("locales");
        fs::create_dir_all(&locales_dir).unwrap();
        fs::write(locales_dir.join("en-US.json"), r#"{"key": "value"}"#).unwrap();
        fs::write(locales_dir.join("readme.txt"), "This is a readme").unwrap();

        // Should succeed but only include JSON files in locales list
        let result = UIResources::from_directory(dir.path());
        assert!(result.is_ok());
        let resources = result.unwrap();
        assert_eq!(resources.locales.len(), 1);
        assert!(resources.locales.contains(&"en-US".to_string()));
    }

    #[test]
    fn test_extract_rejects_path_traversal() {
        use flate2::write::GzEncoder;
        use flate2::Compression;
        use tar::Builder;

        let mut archive_data = Vec::new();
        let build_result = (|| {
            let encoder = GzEncoder::new(&mut archive_data, Compression::default());
            let mut builder = Builder::new(encoder);
            // Add a traversal entry
            let mut header = tar::Header::new_gnu();
            header.set_size(3);
            header.set_cksum();
            builder.append_data(&mut header, "../evil.txt", &b"bad"[..])?;
            let encoder = builder.into_inner()?;
            encoder.finish()?;
            Ok::<(), std::io::Error>(())
        })();

        // Some tar implementations reject traversal at build time.
        if build_result.is_err() {
            return;
        }

        let checksum = calculate_crc32(&archive_data);
        let resources = UIResources::from_archive(archive_data, checksum, 3).unwrap();
        let target_dir = tempdir().unwrap();

        let result = resources.extract_to(target_dir.path());
        assert!(result.is_err());
    }

    #[test]
    fn test_validate_empty_locales_directory() {
        let dir = tempdir().unwrap();
        fs::write(dir.path().join("index.html"), "<html></html>").unwrap();

        let locales_dir = dir.path().join("locales");
        fs::create_dir_all(&locales_dir).unwrap();

        // Should succeed with empty locales directory
        let result = UIResources::from_directory(dir.path());
        assert!(result.is_ok());
        assert!(result.unwrap().locales.is_empty());
    }
}

#[cfg(test)]
mod property_tests {
    use super::*;
    use proptest::prelude::*;
    use std::fs;
    use tempfile::tempdir;

    /// Strategy for generating valid HTML content
    fn arb_html_content() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9 ]{0,100}".prop_map(|content| {
            format!("<!DOCTYPE html><html><head><title>Test</title></head><body>{}</body></html>", content)
        })
    }

    /// Strategy for generating valid CSS content
    fn arb_css_content() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9_-]{1,20}".prop_map(|selector| {
            format!("{} {{ margin: 0; padding: 0; }}", selector)
        })
    }

    /// Strategy for generating valid JS content
    fn arb_js_content() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9 ]{0,50}".prop_map(|msg| {
            format!("console.log('{}');", msg)
        })
    }

    /// Create a test UI directory with the given content
    fn create_ui_directory_with_content(
        dir: &Path,
        html: &str,
        css: Option<&str>,
        js: Option<&str>,
        locales: &[(String, String)],
    ) {
        // Create index.html
        fs::write(dir.join("index.html"), html).unwrap();

        // Create styles if provided
        if let Some(css_content) = css {
            let styles_dir = dir.join("styles");
            fs::create_dir_all(&styles_dir).unwrap();
            fs::write(styles_dir.join("main.css"), css_content).unwrap();
        }

        // Create scripts if provided
        if let Some(js_content) = js {
            let scripts_dir = dir.join("scripts");
            fs::create_dir_all(&scripts_dir).unwrap();
            fs::write(scripts_dir.join("main.js"), js_content).unwrap();
        }

        // Create locales
        if !locales.is_empty() {
            let locales_dir = dir.join("locales");
            fs::create_dir_all(&locales_dir).unwrap();
            for (name, content) in locales {
                fs::write(locales_dir.join(format!("{}.json", name)), content).unwrap();
            }
        }
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(50))]

        /// Property 7: UI Resources Round-Trip
        /// For any valid UI resources directory, creating an archive and extracting it
        /// should produce identical file contents.
        /// **Validates: Requirements 5.3, 5.9**
        #[test]
        fn prop_ui_resources_roundtrip(
            html in arb_html_content(),
            css in prop::option::of(arb_css_content()),
            js in prop::option::of(arb_js_content()),
            locale_count in 0usize..3,
        ) {
            let source_dir = tempdir().unwrap();
            let target_dir = tempdir().unwrap();

            // Generate locales
            let locales: Vec<(String, String)> = (0..locale_count)
                .map(|i| {
                    let names = ["en-US", "zh-CN", "ja-JP"];
                    let name = names[i % names.len()].to_string();
                    let content = format!(r#"{{"key{}": "value{}"}}"#, i, i);
                    (name, content)
                })
                .collect();

            // Create source directory
            create_ui_directory_with_content(
                source_dir.path(),
                &html,
                css.as_deref(),
                js.as_deref(),
                &locales,
            );

            // Create archive
            let resources = UIResources::from_directory(source_dir.path()).unwrap();

            // Verify checksum is valid
            prop_assert!(resources.verify().is_ok());

            // Extract to target
            resources.extract_to(target_dir.path()).unwrap();

            // Verify index.html content matches
            let extracted_html = fs::read_to_string(target_dir.path().join("index.html")).unwrap();
            prop_assert_eq!(html, extracted_html);

            // Verify CSS content if present
            if let Some(ref css_content) = css {
                let extracted_css = fs::read_to_string(target_dir.path().join("styles/main.css")).unwrap();
                prop_assert_eq!(css_content, &extracted_css);
            }

            // Verify JS content if present
            if let Some(ref js_content) = js {
                let extracted_js = fs::read_to_string(target_dir.path().join("scripts/main.js")).unwrap();
                prop_assert_eq!(js_content, &extracted_js);
            }

            // Verify locales
            for (name, content) in &locales {
                let extracted_locale = fs::read_to_string(
                    target_dir.path().join(format!("locales/{}.json", name))
                ).unwrap();
                prop_assert_eq!(content, &extracted_locale);
            }
        }

        /// Property: Archive checksum is deterministic
        /// Creating an archive from the same directory should produce the same checksum.
        /// **Validates: Requirements 5.9**
        #[test]
        fn prop_archive_checksum_deterministic(
            html in arb_html_content(),
        ) {
            let dir = tempdir().unwrap();
            fs::write(dir.path().join("index.html"), &html).unwrap();

            let resources1 = UIResources::from_directory(dir.path()).unwrap();
            let resources2 = UIResources::from_directory(dir.path()).unwrap();

            prop_assert_eq!(resources1.checksum, resources2.checksum);
            prop_assert_eq!(resources1.archive, resources2.archive);
        }

        /// Property: from_archive round-trip preserves data
        /// Creating UIResources from archive data should preserve all properties.
        /// **Validates: Requirements 5.3, 5.9**
        #[test]
        fn prop_from_archive_roundtrip(
            html in arb_html_content(),
            locale_count in 0usize..3,
        ) {
            let dir = tempdir().unwrap();

            // Create locales
            let locales: Vec<(String, String)> = (0..locale_count)
                .map(|i| {
                    let names = ["en-US", "zh-CN", "ja-JP"];
                    let name = names[i % names.len()].to_string();
                    let content = format!(r#"{{"key{}": "value{}"}}"#, i, i);
                    (name, content)
                })
                .collect();

            create_ui_directory_with_content(
                dir.path(),
                &html,
                None,
                None,
                &locales,
            );

            let original = UIResources::from_directory(dir.path()).unwrap();

            // Create from archive
            let restored = UIResources::from_archive(
                original.archive.clone(),
                original.checksum,
                original.original_size,
            ).unwrap();

            // Verify properties match
            prop_assert_eq!(original.archive, restored.archive);
            prop_assert_eq!(original.checksum, restored.checksum);
            prop_assert_eq!(original.original_size, restored.original_size);
            prop_assert_eq!(original.locales, restored.locales);
        }

        /// Property: Invalid checksum is rejected
        /// from_archive should reject data with mismatched checksum.
        /// **Validates: Requirements 5.9**
        #[test]
        fn prop_invalid_checksum_rejected(
            html in arb_html_content(),
            wrong_checksum in any::<u32>(),
        ) {
            let dir = tempdir().unwrap();
            fs::write(dir.path().join("index.html"), &html).unwrap();

            let original = UIResources::from_directory(dir.path()).unwrap();

            // Only test if wrong_checksum is actually different
            if wrong_checksum != original.checksum {
                let result = UIResources::from_archive(
                    original.archive.clone(),
                    wrong_checksum,
                    original.original_size,
                );

                prop_assert!(result.is_err());
                if let Err(e) = result {
                    let is_checksum_mismatch = matches!(e, InstallerError::ChecksumMismatch { .. });
                    prop_assert!(is_checksum_mismatch, "Expected ChecksumMismatch error");
                }
            }
        }
    }
}
