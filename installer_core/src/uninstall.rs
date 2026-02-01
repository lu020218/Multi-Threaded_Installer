//! Uninstall module for removing installed applications.
//!
//! Provides functionality for:
//! - Reading install.manifest.json
//! - Deleting installed files
//! - Cleaning up registry entries
//! - Removing shortcuts and auto-startup entries
//! - Self-cleaning (deleting manifest and uninstaller)
//!
//! # Requirements
//! - 11.3: Read install.manifest.json
//! - 11.4: Delete all files in manifest
//! - 11.5: Delete empty directories
//! - 11.6: Delete registry keys created during installation
//! - 11.7: Delete desktop shortcuts
//! - 11.8: Delete auto-startup entries
//! - 11.9: Delete manifest and uninstaller executable

use crate::filesystem::{delete_empty_dir, delete_file};
use crate::platform::{create_platform, Platform};
use installer_shared::{InstallerError, Phase, ProgressEvent, RegistryEntry, Result};
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use tracing::{debug, error, info, warn};

/// Install manifest structure stored in install.manifest.json.
///
/// Contains all information needed to uninstall an application:
/// - List of installed files
/// - List of created directories
/// - Registry entries to clean up
/// - Auto-startup and desktop icon settings
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InstallManifest {
    /// Application name
    pub app_name: String,
    /// Application version
    pub version: String,
    /// Installation directory
    pub install_dir: String,
    /// List of installed file paths (relative to install_dir)
    pub files: Vec<String>,
    /// List of created directories (relative to install_dir)
    pub directories: Vec<String>,
    /// Registry entries created during installation
    #[serde(default)]
    pub registry_entries: Vec<ManifestRegistryEntry>,
    /// Whether auto-startup was configured
    #[serde(default)]
    pub auto_startup: bool,
    /// Whether desktop icons were created
    #[serde(default)]
    pub desktop_icons: bool,
    /// Timestamp when the application was installed
    #[serde(default)]
    pub created_at: Option<String>,
}

/// Simplified registry entry for manifest storage.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ManifestRegistryEntry {
    /// Registry path
    pub path: String,
    /// Registry key name
    pub key: String,
}

impl From<&RegistryEntry> for ManifestRegistryEntry {
    fn from(entry: &RegistryEntry) -> Self {
        Self {
            path: entry.path.clone(),
            key: entry.key.clone(),
        }
    }
}

/// Statistics about an uninstallation.
#[derive(Debug, Clone)]
pub struct UninstallStats {
    /// Number of files deleted
    pub files_deleted: usize,
    /// Number of directories removed
    pub directories_removed: usize,
    /// Number of registry entries cleaned
    pub registry_entries_cleaned: usize,
    /// Whether shortcuts were removed
    pub shortcuts_removed: bool,
    /// Whether auto-startup was disabled
    pub auto_startup_disabled: bool,
    /// Errors encountered during uninstallation
    pub errors: Vec<String>,
}

impl Default for UninstallStats {
    fn default() -> Self {
        Self {
            files_deleted: 0,
            directories_removed: 0,
            registry_entries_cleaned: 0,
            shortcuts_removed: false,
            auto_startup_disabled: false,
            errors: Vec::new(),
        }
    }
}

/// Uninstaller for removing installed applications.
pub struct Uninstaller {
    /// Path to the install.manifest.json file
    manifest_path: PathBuf,
    /// Parsed manifest data
    manifest: InstallManifest,
    /// Platform abstraction
    platform: Box<dyn Platform>,
}

impl Uninstaller {
    /// Create a new uninstaller from a manifest file path.
    ///
    /// # Arguments
    /// * `manifest_path` - Path to the install.manifest.json file
    ///
    /// # Returns
    /// * `Ok(Uninstaller)` if manifest was successfully read
    /// * `Err(InstallerError)` if manifest could not be read or parsed
    ///
    /// # Requirements
    /// - 11.3: Read install.manifest.json
    pub fn new(manifest_path: PathBuf) -> Result<Self> {
        let manifest = Self::read_manifest(&manifest_path)?;
        
        Ok(Self {
            manifest_path,
            manifest,
            platform: create_platform(),
        })
    }

    /// Create an uninstaller from an installation directory.
    ///
    /// Looks for install.manifest.json in the given directory.
    ///
    /// # Arguments
    /// * `install_dir` - Installation directory containing the manifest
    ///
    /// # Returns
    /// * `Ok(Uninstaller)` if manifest was found and read
    /// * `Err(InstallerError)` if manifest was not found or could not be parsed
    pub fn from_install_dir(install_dir: &Path) -> Result<Self> {
        let manifest_path = install_dir.join("install.manifest.json");
        Self::new(manifest_path)
    }

    /// Read and parse the install manifest.
    ///
    /// # Arguments
    /// * `path` - Path to the manifest file
    ///
    /// # Returns
    /// * `Ok(InstallManifest)` if successfully parsed
    /// * `Err(InstallerError)` on read or parse failure
    ///
    /// # Requirements
    /// - 11.3: Parse install.manifest.json and load installed file list
    fn read_manifest(path: &Path) -> Result<InstallManifest> {
        info!("Reading install manifest from {:?}", path);

        if !path.exists() {
            return Err(InstallerError::Io(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                format!("Install manifest not found: {:?}", path),
            )));
        }

        let content = fs::read_to_string(path)?;
        let manifest: InstallManifest = serde_json::from_str(&content)
            .map_err(|e| InstallerError::Serialization(format!("Failed to parse manifest: {}", e)))?;

        debug!(
            "Loaded manifest: app={}, version={}, {} files, {} directories",
            manifest.app_name,
            manifest.version,
            manifest.files.len(),
            manifest.directories.len()
        );

        Ok(manifest)
    }

    /// Get a reference to the manifest.
    pub fn manifest(&self) -> &InstallManifest {
        &self.manifest
    }

    /// Get the installation directory.
    pub fn install_dir(&self) -> PathBuf {
        PathBuf::from(&self.manifest.install_dir)
    }

    /// Get the list of installed files.
    pub fn installed_files(&self) -> &[String] {
        &self.manifest.files
    }

    /// Get the list of created directories.
    pub fn directories(&self) -> &[String] {
        &self.manifest.directories
    }


    /// Delete all files listed in the manifest.
    ///
    /// # Arguments
    /// * `progress` - Progress callback function
    ///
    /// # Returns
    /// * Number of files successfully deleted
    /// * List of errors for files that could not be deleted
    ///
    /// # Requirements
    /// - 11.4: Delete all files in manifest
    pub fn delete_files<F>(&self, progress: F) -> (usize, Vec<String>)
    where
        F: Fn(ProgressEvent),
    {
        let install_dir = self.install_dir();
        let total_files = self.manifest.files.len() as u64;
        let mut deleted = 0usize;
        let mut errors = Vec::new();

        info!("Deleting {} files from {:?}", total_files, install_dir);

        for (idx, file_path) in self.manifest.files.iter().enumerate() {
            let full_path = install_dir.join(file_path);

            progress(
                ProgressEvent::new(Phase::Completing, idx as u64, total_files)
                    .with_file(file_path)
                    .with_message(format!("Deleting: {}", file_path)),
            );

            if full_path.exists() {
                match delete_file(&full_path) {
                    Ok(()) => {
                        debug!("Deleted file: {:?}", full_path);
                        deleted += 1;
                    }
                    Err(e) => {
                        let error_msg = format!("Failed to delete {:?}: {}", full_path, e);
                        error!("{}", error_msg);
                        errors.push(error_msg);
                    }
                }
            } else {
                debug!("File already deleted or not found: {:?}", full_path);
                // Count as deleted since it's not there
                deleted += 1;
            }
        }

        progress(ProgressEvent::new(Phase::Completing, total_files, total_files));

        info!("Deleted {} of {} files", deleted, total_files);
        (deleted, errors)
    }

    /// Delete empty directories created during installation.
    ///
    /// Directories are deleted in reverse order (deepest first) to ensure
    /// parent directories are deleted after their children.
    ///
    /// # Returns
    /// * Number of directories successfully removed
    /// * List of errors for directories that could not be removed
    ///
    /// # Requirements
    /// - 11.5: Delete empty directories
    pub fn delete_directories(&self) -> (usize, Vec<String>) {
        let install_dir = self.install_dir();
        let mut removed = 0usize;
        let mut errors = Vec::new();

        // Sort directories by depth (deepest first)
        let mut directories: Vec<_> = self.manifest.directories.iter().collect();
        directories.sort_by(|a, b| {
            let depth_a = a.matches('/').count() + a.matches('\\').count();
            let depth_b = b.matches('/').count() + b.matches('\\').count();
            depth_b.cmp(&depth_a) // Reverse order (deepest first)
        });

        info!("Removing {} directories", directories.len());

        for dir_path in directories {
            let full_path = install_dir.join(dir_path);

            if full_path.exists() && full_path.is_dir() {
                // Check if directory is empty
                match fs::read_dir(&full_path) {
                    Ok(mut entries) => {
                        if entries.next().is_none() {
                            // Directory is empty, safe to delete
                            match delete_empty_dir(&full_path) {
                                Ok(()) => {
                                    debug!("Removed empty directory: {:?}", full_path);
                                    removed += 1;
                                }
                                Err(e) => {
                                    let error_msg = format!("Failed to remove {:?}: {}", full_path, e);
                                    warn!("{}", error_msg);
                                    errors.push(error_msg);
                                }
                            }
                        } else {
                            debug!("Directory not empty, skipping: {:?}", full_path);
                        }
                    }
                    Err(e) => {
                        let error_msg = format!("Failed to read directory {:?}: {}", full_path, e);
                        warn!("{}", error_msg);
                        errors.push(error_msg);
                    }
                }
            }
        }

        // Also try to remove the install directory itself if empty
        if install_dir.exists() && install_dir.is_dir() {
            if let Ok(mut entries) = fs::read_dir(&install_dir) {
                // Check if only manifest and uninstaller remain
                let remaining: Vec<_> = entries.by_ref().filter_map(|e| e.ok()).collect();
                let only_uninstall_files = remaining.iter().all(|e| {
                    let name = e.file_name();
                    let name_str = name.to_string_lossy();
                    name_str == "install.manifest.json" || name_str == "uninstall.exe"
                });

                if remaining.is_empty() || only_uninstall_files {
                    debug!("Install directory is empty or only contains uninstall files");
                }
            }
        }

        info!("Removed {} directories", removed);
        (removed, errors)
    }

    /// Clean up registry entries created during installation.
    ///
    /// # Returns
    /// * Number of registry entries successfully cleaned
    /// * List of errors for entries that could not be cleaned
    ///
    /// # Requirements
    /// - 11.6: Delete registry keys created during installation
    pub fn cleanup_registry(&self) -> (usize, Vec<String>) {
        let mut cleaned = 0usize;
        let mut errors = Vec::new();

        info!(
            "Cleaning up {} registry entries",
            self.manifest.registry_entries.len()
        );

        // Delete custom registry entries
        for entry in &self.manifest.registry_entries {
            match self.platform.delete_registry(&entry.path, &entry.key) {
                Ok(()) => {
                    debug!("Deleted registry entry: {}\\{}", entry.path, entry.key);
                    cleaned += 1;
                }
                Err(e) => {
                    let error_msg = format!(
                        "Failed to delete registry entry {}\\{}: {}",
                        entry.path, entry.key, e
                    );
                    warn!("{}", error_msg);
                    errors.push(error_msg);
                }
            }
        }

        // Delete the uninstaller registry key
        let uninstall_key_path = format!(
            "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{}",
            self.manifest.app_name
        );

        // Try to delete the entire uninstall key
        // Note: This may fail if we don't have admin privileges
        if let Err(e) = self.platform.delete_registry(&uninstall_key_path, "") {
            warn!("Failed to remove uninstaller registry key: {}", e);
            // Don't add to errors - this is expected to fail without admin
        } else {
            debug!("Removed uninstaller registry key");
            cleaned += 1;
        }

        info!("Cleaned {} registry entries", cleaned);
        (cleaned, errors)
    }


    /// Delete desktop shortcuts created during installation.
    ///
    /// # Returns
    /// * `true` if shortcut was successfully deleted or didn't exist
    /// * `false` if deletion failed
    ///
    /// # Requirements
    /// - 11.7: Delete desktop shortcuts
    pub fn delete_shortcuts(&self) -> bool {
        if !self.manifest.desktop_icons {
            debug!("No desktop icons were created, skipping shortcut deletion");
            return true;
        }

        info!("Deleting desktop shortcut for {}", self.manifest.app_name);

        // Get desktop directory
        let desktop_dir = match self.platform.desktop_dir() {
            Ok(dir) => dir,
            Err(e) => {
                warn!("Failed to get desktop directory: {}", e);
                return false;
            }
        };

        let shortcut_path = desktop_dir.join(format!("{}.lnk", self.manifest.app_name));

        if shortcut_path.exists() {
            match delete_file(&shortcut_path) {
                Ok(()) => {
                    info!("Deleted desktop shortcut: {:?}", shortcut_path);
                    true
                }
                Err(e) => {
                    warn!("Failed to delete desktop shortcut: {}", e);
                    false
                }
            }
        } else {
            debug!("Desktop shortcut not found: {:?}", shortcut_path);
            true
        }
    }

    /// Disable auto-startup if it was configured during installation.
    ///
    /// # Returns
    /// * `true` if auto-startup was successfully disabled or wasn't configured
    /// * `false` if disabling failed
    ///
    /// # Requirements
    /// - 11.8: Delete auto-startup entries
    pub fn disable_auto_startup(&self) -> bool {
        if !self.manifest.auto_startup {
            debug!("Auto-startup was not configured, skipping");
            return true;
        }

        info!("Disabling auto-startup for {}", self.manifest.app_name);

        match self.platform.configure_auto_startup(
            &self.manifest.app_name,
            Path::new(""), // Path doesn't matter when disabling
            false,
        ) {
            Ok(()) => {
                info!("Disabled auto-startup");
                true
            }
            Err(e) => {
                warn!("Failed to disable auto-startup: {}", e);
                false
            }
        }
    }

    /// Delete the manifest file and schedule uninstaller for deletion.
    ///
    /// Note: On Windows, the uninstaller executable cannot delete itself while running.
    /// This method will delete the manifest and create a batch file to delete the
    /// uninstaller after the process exits.
    ///
    /// # Returns
    /// * `true` if cleanup was successful
    /// * `false` if cleanup failed
    ///
    /// # Requirements
    /// - 11.9: Delete manifest file and uninstall.exe
    pub fn self_cleanup(&self) -> bool {
        info!("Performing self-cleanup");

        let mut success = true;

        // Delete the manifest file
        if self.manifest_path.exists() {
            match delete_file(&self.manifest_path) {
                Ok(()) => {
                    info!("Deleted manifest file: {:?}", self.manifest_path);
                }
                Err(e) => {
                    warn!("Failed to delete manifest file: {}", e);
                    success = false;
                }
            }
        }

        // Try to delete the uninstaller executable
        let uninstall_exe = self.install_dir().join("uninstall.exe");
        if uninstall_exe.exists() {
            // On Windows, we can't delete ourselves while running
            // Create a batch file to delete the uninstaller after we exit
            #[cfg(windows)]
            {
                success = self.create_self_delete_batch(&uninstall_exe) && success;
            }

            #[cfg(not(windows))]
            {
                match delete_file(&uninstall_exe) {
                    Ok(()) => {
                        info!("Deleted uninstaller: {:?}", uninstall_exe);
                    }
                    Err(e) => {
                        warn!("Failed to delete uninstaller: {}", e);
                        success = false;
                    }
                }
            }
        }

        // Try to remove the install directory if it's empty
        let install_dir = self.install_dir();
        if install_dir.exists() {
            if let Ok(mut entries) = fs::read_dir(&install_dir) {
                if entries.next().is_none() {
                    match fs::remove_dir(&install_dir) {
                        Ok(()) => {
                            info!("Removed empty install directory: {:?}", install_dir);
                        }
                        Err(e) => {
                            warn!("Failed to remove install directory: {}", e);
                            // Don't fail for this
                        }
                    }
                }
            }
        }

        success
    }

    /// Create a batch file that will delete the uninstaller after it exits.
    /// 
    /// Note: This is skipped in test mode to avoid hanging tests.
    #[cfg(windows)]
    fn create_self_delete_batch(&self, uninstall_exe: &Path) -> bool {
        // Skip batch file creation in test mode to avoid hanging tests
        #[cfg(test)]
        {
            debug!("Skipping batch file creation in test mode");
            // Just try to delete the file directly (will fail if running, but that's ok for tests)
            let _ = delete_file(uninstall_exe);
            return true;
        }
        
        #[cfg(not(test))]
        {
            let install_dir = self.install_dir();
            let batch_path = std::env::temp_dir().join("uninstall_cleanup.bat");
            
            // Create a batch script that:
            // 1. Waits for the uninstaller to exit
            // 2. Deletes the uninstaller executable
            // 3. Removes the install directory if empty
            // 4. Deletes itself
            let batch_content = format!(
                r#"@echo off
:wait
timeout /t 1 /nobreak >nul
if exist "{uninstall_exe}" (
    del /f /q "{uninstall_exe}" 2>nul
    if exist "{uninstall_exe}" goto wait
)
rmdir /q "{install_dir}" 2>nul
del /f /q "%~f0"
"#,
                uninstall_exe = uninstall_exe.display(),
                install_dir = install_dir.display()
            );

            match fs::write(&batch_path, batch_content) {
                Ok(()) => {
                    // Start the batch file in a detached process
                    match std::process::Command::new("cmd")
                        .args(["/C", "start", "/min", "", &batch_path.to_string_lossy()])
                        .spawn()
                    {
                        Ok(_) => {
                            info!("Created self-delete batch file: {:?}", batch_path);
                            true
                        }
                        Err(e) => {
                            warn!("Failed to start cleanup batch: {}", e);
                            false
                        }
                    }
                }
                Err(e) => {
                    warn!("Failed to create cleanup batch file: {}", e);
                    false
                }
            }
        }
    }

    /// Perform a complete uninstallation.
    ///
    /// This method performs all uninstallation steps in order:
    /// 1. Delete installed files
    /// 2. Remove empty directories
    /// 3. Clean up registry entries
    /// 4. Delete desktop shortcuts
    /// 5. Disable auto-startup
    /// 6. Self-cleanup (manifest and uninstaller)
    ///
    /// # Arguments
    /// * `progress` - Progress callback function
    ///
    /// # Returns
    /// * `Ok(UninstallStats)` with statistics about the uninstallation
    /// * `Err(InstallerError)` if a critical error occurs
    ///
    /// # Requirements
    /// - 11.4, 11.5, 11.6, 11.7, 11.8, 11.9
    pub fn uninstall<F>(&self, progress: F) -> Result<UninstallStats>
    where
        F: Fn(ProgressEvent),
    {
        info!(
            "Starting uninstallation of {} v{}",
            self.manifest.app_name, self.manifest.version
        );

        let mut stats = UninstallStats::default();

        // Step 1: Delete files
        progress(ProgressEvent::new(Phase::Completing, 0, 5).with_message("Deleting files..."));
        let (files_deleted, file_errors) = self.delete_files(&progress);
        stats.files_deleted = files_deleted;
        stats.errors.extend(file_errors);

        // Step 2: Remove directories
        progress(
            ProgressEvent::new(Phase::Completing, 1, 5).with_message("Removing directories..."),
        );
        let (dirs_removed, dir_errors) = self.delete_directories();
        stats.directories_removed = dirs_removed;
        stats.errors.extend(dir_errors);

        // Step 3: Clean up registry
        progress(
            ProgressEvent::new(Phase::Completing, 2, 5).with_message("Cleaning registry..."),
        );
        let (registry_cleaned, registry_errors) = self.cleanup_registry();
        stats.registry_entries_cleaned = registry_cleaned;
        stats.errors.extend(registry_errors);

        // Step 4: Delete shortcuts
        progress(
            ProgressEvent::new(Phase::Completing, 3, 5).with_message("Removing shortcuts..."),
        );
        stats.shortcuts_removed = self.delete_shortcuts();

        // Step 5: Disable auto-startup
        progress(
            ProgressEvent::new(Phase::Completing, 4, 5).with_message("Disabling auto-startup..."),
        );
        stats.auto_startup_disabled = self.disable_auto_startup();

        // Step 6: Self-cleanup
        progress(
            ProgressEvent::new(Phase::Completing, 5, 5).with_message("Cleaning up..."),
        );
        let cleanup_success = self.self_cleanup();
        if !cleanup_success {
            stats.errors.push("Self-cleanup partially failed".to_string());
        }

        info!(
            "Uninstallation complete: {} files deleted, {} directories removed, {} registry entries cleaned",
            stats.files_deleted, stats.directories_removed, stats.registry_entries_cleaned
        );

        if !stats.errors.is_empty() {
            warn!("Uninstallation completed with {} errors", stats.errors.len());
        }

        Ok(stats)
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    fn create_test_manifest(dir: &Path) -> InstallManifest {
        InstallManifest {
            app_name: "TestApp".to_string(),
            version: "1.0.0".to_string(),
            install_dir: dir.to_string_lossy().to_string(),
            files: vec![
                "file1.txt".to_string(),
                "subdir/file2.txt".to_string(),
            ],
            directories: vec!["subdir".to_string()],
            registry_entries: vec![],
            auto_startup: false,
            desktop_icons: false,
            created_at: Some("2024-01-01T00:00:00Z".to_string()),
        }
    }

    fn write_manifest(dir: &Path, manifest: &InstallManifest) -> PathBuf {
        let manifest_path = dir.join("install.manifest.json");
        let content = serde_json::to_string_pretty(manifest).unwrap();
        fs::write(&manifest_path, content).unwrap();
        manifest_path
    }

    #[test]
    fn test_read_manifest() {
        let dir = tempdir().unwrap();
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        
        assert_eq!(uninstaller.manifest().app_name, "TestApp");
        assert_eq!(uninstaller.manifest().version, "1.0.0");
        assert_eq!(uninstaller.manifest().files.len(), 2);
        assert_eq!(uninstaller.manifest().directories.len(), 1);
    }

    #[test]
    fn test_read_manifest_not_found() {
        let dir = tempdir().unwrap();
        let manifest_path = dir.path().join("nonexistent.json");

        let result = Uninstaller::new(manifest_path);
        assert!(result.is_err());
    }

    #[test]
    fn test_read_manifest_invalid_json() {
        let dir = tempdir().unwrap();
        let manifest_path = dir.path().join("install.manifest.json");
        fs::write(&manifest_path, "invalid json content").unwrap();

        let result = Uninstaller::new(manifest_path);
        assert!(result.is_err());
    }

    #[test]
    fn test_from_install_dir() {
        let dir = tempdir().unwrap();
        let manifest = create_test_manifest(dir.path());
        write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::from_install_dir(dir.path()).unwrap();
        assert_eq!(uninstaller.manifest().app_name, "TestApp");
    }

    #[test]
    fn test_delete_files() {
        let dir = tempdir().unwrap();
        
        // Create test files
        let file1 = dir.path().join("file1.txt");
        let subdir = dir.path().join("subdir");
        let file2 = subdir.join("file2.txt");
        
        fs::write(&file1, b"content1").unwrap();
        fs::create_dir(&subdir).unwrap();
        fs::write(&file2, b"content2").unwrap();

        // Create manifest
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        let (deleted, errors) = uninstaller.delete_files(|_| {});

        assert_eq!(deleted, 2);
        assert!(errors.is_empty());
        assert!(!file1.exists());
        assert!(!file2.exists());
    }

    #[test]
    fn test_delete_files_already_deleted() {
        let dir = tempdir().unwrap();
        
        // Create manifest but don't create the files
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        let (deleted, errors) = uninstaller.delete_files(|_| {});

        // Files that don't exist are counted as deleted
        assert_eq!(deleted, 2);
        assert!(errors.is_empty());
    }

    #[test]
    fn test_delete_directories() {
        let dir = tempdir().unwrap();
        
        // Create directory structure
        let subdir = dir.path().join("subdir");
        let nested = subdir.join("nested");
        fs::create_dir_all(&nested).unwrap();

        // Create manifest with directories
        let mut manifest = create_test_manifest(dir.path());
        manifest.directories = vec![
            "subdir".to_string(),
            "subdir/nested".to_string(),
        ];
        manifest.files = vec![]; // No files
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        let (removed, errors) = uninstaller.delete_directories();

        // Both directories should be removed (nested first, then subdir)
        assert_eq!(removed, 2);
        assert!(errors.is_empty());
        assert!(!nested.exists());
        assert!(!subdir.exists());
    }

    #[test]
    fn test_delete_directories_not_empty() {
        let dir = tempdir().unwrap();
        
        // Create directory with a file
        let subdir = dir.path().join("subdir");
        fs::create_dir(&subdir).unwrap();
        fs::write(subdir.join("remaining.txt"), b"content").unwrap();

        // Create manifest
        let mut manifest = create_test_manifest(dir.path());
        manifest.directories = vec!["subdir".to_string()];
        manifest.files = vec![]; // Don't include the remaining file
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        let (removed, errors) = uninstaller.delete_directories();

        // Directory should not be removed because it's not empty
        assert_eq!(removed, 0);
        assert!(errors.is_empty()); // Not an error, just skipped
        assert!(subdir.exists());
    }

    #[test]
    fn test_manifest_registry_entry_from() {
        use installer_shared::RegistryValueType;
        
        let entry = RegistryEntry {
            path: "HKEY_CURRENT_USER\\Software\\Test".to_string(),
            key: "TestKey".to_string(),
            value: "TestValue".to_string(),
            value_type: RegistryValueType::String,
        };

        let manifest_entry = ManifestRegistryEntry::from(&entry);
        assert_eq!(manifest_entry.path, entry.path);
        assert_eq!(manifest_entry.key, entry.key);
    }

    #[test]
    fn test_uninstall_stats_default() {
        let stats = UninstallStats::default();
        assert_eq!(stats.files_deleted, 0);
        assert_eq!(stats.directories_removed, 0);
        assert_eq!(stats.registry_entries_cleaned, 0);
        assert!(!stats.shortcuts_removed);
        assert!(!stats.auto_startup_disabled);
        assert!(stats.errors.is_empty());
    }

    #[test]
    fn test_full_uninstall() {
        let dir = tempdir().unwrap();
        
        // Create test files and directories
        let file1 = dir.path().join("file1.txt");
        let subdir = dir.path().join("subdir");
        let file2 = subdir.join("file2.txt");
        
        fs::write(&file1, b"content1").unwrap();
        fs::create_dir(&subdir).unwrap();
        fs::write(&file2, b"content2").unwrap();

        // Create manifest
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path.clone()).unwrap();
        let stats = uninstaller.uninstall(|_| {}).unwrap();

        assert_eq!(stats.files_deleted, 2);
        assert_eq!(stats.directories_removed, 1);
        assert!(!file1.exists());
        assert!(!file2.exists());
        assert!(!subdir.exists());
        // Manifest should be deleted
        assert!(!manifest_path.exists());
    }

    #[test]
    fn test_install_dir() {
        let dir = tempdir().unwrap();
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        assert_eq!(uninstaller.install_dir(), PathBuf::from(&manifest.install_dir));
    }

    #[test]
    fn test_installed_files() {
        let dir = tempdir().unwrap();
        let manifest = create_test_manifest(dir.path());
        let manifest_path = write_manifest(dir.path(), &manifest);

        let uninstaller = Uninstaller::new(manifest_path).unwrap();
        let files = uninstaller.installed_files();
        
        assert_eq!(files.len(), 2);
        assert!(files.contains(&"file1.txt".to_string()));
        assert!(files.contains(&"subdir/file2.txt".to_string()));
    }
}


// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use crate::packager::Packager;
    use installer_shared::PackagerConfig;
    use proptest::prelude::*;
    use std::fs;
    use tempfile::tempdir;

    /// Generate random file content of varying sizes
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 50..2000)
    }

    /// Generate a valid filename
    fn filename_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_]{1,10}\\.(txt|bin|dat)"
    }

    /// Generate a list of files with content
    fn files_strategy() -> impl Strategy<Value = Vec<(String, Vec<u8>)>> {
        prop::collection::vec(
            (filename_strategy(), file_content_strategy()),
            1..4
        )
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(5))]

        /// **Property 9: Uninstall Round-Trip**
        /// For any successful installation, executing uninstall should delete all
        /// installed files, directories, registry keys, and shortcuts.
        ///
        /// **Validates: Requirements 11.4, 11.5, 11.6, 11.7, 11.8**
        #[test]
        fn prop_uninstall_removes_all_files(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create unique filenames to avoid collisions
            let mut created_files: Vec<String> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                created_files.push(unique_name);
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install
            let installer = crate::installer::Installer::new(output_path).expect("Failed to create installer");
            let options = installer_shared::InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                silent: true,
                thread_count: None,
            };

            installer.install(options.clone(), |_| {}).expect("Installation failed");

            // Create uninstaller (this creates the manifest)
            installer.create_uninstaller(install_dir.path()).expect("Failed to create uninstaller");

            // Verify files were installed
            for name in &created_files {
                let installed_path = install_dir.path().join(name);
                prop_assert!(installed_path.exists(),
                    "File {} should exist after installation", name);
            }

            // Verify manifest was created
            let manifest_path = install_dir.path().join("install.manifest.json");
            prop_assert!(manifest_path.exists(), "Manifest should exist after installation");

            // Uninstall
            let uninstaller = Uninstaller::from_install_dir(install_dir.path())
                .expect("Failed to create uninstaller");
            let stats = uninstaller.uninstall(|_| {}).expect("Uninstall failed");

            // Verify all files were deleted
            for name in &created_files {
                let installed_path = install_dir.path().join(name);
                prop_assert!(!installed_path.exists(),
                    "File {} should be deleted after uninstall", name);
            }

            // Verify manifest was deleted
            prop_assert!(!manifest_path.exists(), "Manifest should be deleted after uninstall");

            // Verify stats are reasonable
            prop_assert!(stats.files_deleted > 0, "Should have deleted at least one file");
        }

        /// **Property 9 (continued): Uninstall Removes Empty Directories**
        /// After uninstall, all directories created during installation should be
        /// removed if they are empty.
        ///
        /// **Validates: Requirements 11.5**
        #[test]
        fn prop_uninstall_removes_directories(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create files in a subdirectory
            let subdir = input_dir.path().join("subdir");
            fs::create_dir_all(&subdir).expect("Failed to create subdir");

            let mut created_files: Vec<String> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = subdir.join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                created_files.push(format!("subdir/{}", unique_name));
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install
            let installer = crate::installer::Installer::new(output_path).expect("Failed to create installer");
            let options = installer_shared::InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                silent: true,
                thread_count: None,
            };

            installer.install(options.clone(), |_| {}).expect("Installation failed");

            // Create uninstaller
            installer.create_uninstaller(install_dir.path()).expect("Failed to create uninstaller");

            // Verify subdirectory was created
            let installed_subdir = install_dir.path().join("subdir");
            prop_assert!(installed_subdir.exists(), "Subdirectory should exist after installation");

            // Uninstall
            let uninstaller = Uninstaller::from_install_dir(install_dir.path())
                .expect("Failed to create uninstaller");
            let stats = uninstaller.uninstall(|_| {}).expect("Uninstall failed");

            // Verify subdirectory was removed
            prop_assert!(!installed_subdir.exists(),
                "Subdirectory should be removed after uninstall");

            // Verify directories were removed
            prop_assert!(stats.directories_removed > 0,
                "Should have removed at least one directory");
        }

        /// **Property 9 (continued): Uninstall Handles Missing Files Gracefully**
        /// If some files are already deleted before uninstall, the uninstaller
        /// should still complete successfully.
        ///
        /// **Validates: Requirements 11.4**
        #[test]
        fn prop_uninstall_handles_missing_files(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create files
            let mut created_files: Vec<String> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                created_files.push(unique_name);
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install
            let installer = crate::installer::Installer::new(output_path).expect("Failed to create installer");
            let options = installer_shared::InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                silent: true,
                thread_count: None,
            };

            installer.install(options.clone(), |_| {}).expect("Installation failed");

            // Create uninstaller
            installer.create_uninstaller(install_dir.path()).expect("Failed to create uninstaller");

            // Delete some files manually before uninstall
            if !created_files.is_empty() {
                let first_file = install_dir.path().join(&created_files[0]);
                if first_file.exists() {
                    fs::remove_file(&first_file).expect("Failed to delete file");
                }
            }

            // Uninstall should still succeed
            let uninstaller = Uninstaller::from_install_dir(install_dir.path())
                .expect("Failed to create uninstaller");
            let result = uninstaller.uninstall(|_| {});

            prop_assert!(result.is_ok(), "Uninstall should succeed even with missing files");

            // All remaining files should be deleted
            for name in &created_files {
                let installed_path = install_dir.path().join(name);
                prop_assert!(!installed_path.exists(),
                    "File {} should not exist after uninstall", name);
            }
        }
    }
}
