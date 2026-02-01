//! UI resource loading and cleanup module.
//!
//! This module handles:
//! - Extracting embedded UI resources to a temporary directory
//! - Loading HTML/CSS/JS from the temporary directory
//! - Cleaning up temporary resources on exit
//!
//! # Requirements
//! - 5.4: Extract UI resources to temporary directory
//! - 5.5: Load HTML pages from extracted directory
//! - 5.6: Clean up temporary UI resources on exit

use installer_core::Installer;
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use tracing::{debug, error, info, warn};

lazy_static::lazy_static! {
    /// Global state for tracking temporary UI resources directory
    static ref UI_TEMP_DIR: Mutex<Option<PathBuf>> = Mutex::new(None);
}

/// Result of UI resource extraction
#[derive(Debug)]
pub struct ExtractedUIResources {
    /// Path to the temporary directory containing UI resources
    pub temp_dir: PathBuf,
    /// Path to the index.html file
    pub index_path: PathBuf,
    /// Available locales
    pub locales: Vec<String>,
}

/// Extract UI resources from the installer package.
///
/// If the package contains embedded UI resources, they are extracted to a
/// temporary directory. If not, returns None and the default UI should be used.
///
/// # Arguments
/// * `package_path` - Path to the installer package
///
/// # Returns
/// * `Ok(Some(ExtractedUIResources))` if UI resources were extracted
/// * `Ok(None)` if the package doesn't contain UI resources
/// * `Err(String)` on extraction failure
///
/// # Requirements
/// - 5.4: Extract UI resources to temporary directory
pub fn extract_ui_resources(package_path: &Path) -> Result<Option<ExtractedUIResources>, String> {
    info!("Checking for embedded UI resources in: {:?}", package_path);
    
    // Create installer to parse package
    let installer = Installer::new(package_path.to_path_buf())
        .map_err(|e| format!("Failed to open package: {}", e))?;
    
    // Check if package has UI resources
    let has_ui = installer.has_ui_resources()
        .map_err(|e| format!("Failed to check UI resources: {}", e))?;
    
    if !has_ui {
        info!("Package does not contain embedded UI resources");
        return Ok(None);
    }
    
    // Create temporary directory
    let temp_dir = create_temp_dir()?;
    info!("Extracting UI resources to: {:?}", temp_dir);
    
    // Extract UI resources
    let ui_resources = installer.extract_ui_resources(&temp_dir)
        .map_err(|e| format!("Failed to extract UI resources: {}", e))?;
    
    let ui_resources = match ui_resources {
        Some(r) => r,
        None => {
            warn!("Package reported UI resources but extraction returned None");
            cleanup_temp_dir(&temp_dir);
            return Ok(None);
        }
    };
    
    // Verify index.html exists
    let index_path = temp_dir.join("index.html");
    if !index_path.exists() {
        error!("Extracted UI resources missing index.html");
        cleanup_temp_dir(&temp_dir);
        return Err("UI resources missing index.html".to_string());
    }
    
    // Store temp dir for cleanup
    if let Ok(mut guard) = UI_TEMP_DIR.lock() {
        *guard = Some(temp_dir.clone());
    }
    
    Ok(Some(ExtractedUIResources {
        temp_dir,
        index_path,
        locales: ui_resources.locales,
    }))
}

/// Create a temporary directory for UI resources.
fn create_temp_dir() -> Result<PathBuf, String> {
    let temp_base = std::env::temp_dir();
    let temp_name = format!("installer_ui_{}", std::process::id());
    let temp_dir = temp_base.join(temp_name);
    
    std::fs::create_dir_all(&temp_dir)
        .map_err(|e| format!("Failed to create temp directory: {}", e))?;
    
    Ok(temp_dir)
}

/// Clean up a temporary directory.
fn cleanup_temp_dir(path: &Path) {
    if path.exists() {
        if let Err(e) = std::fs::remove_dir_all(path) {
            warn!("Failed to clean up temp directory {:?}: {}", path, e);
        } else {
            debug!("Cleaned up temp directory: {:?}", path);
        }
    }
}

/// Clean up all temporary UI resources.
///
/// This should be called when the application exits.
///
/// # Requirements
/// - 5.6: Clean up temporary UI resources on exit
pub fn cleanup_ui_resources() {
    if let Ok(mut guard) = UI_TEMP_DIR.lock() {
        if let Some(ref temp_dir) = *guard {
            info!("Cleaning up UI resources at: {:?}", temp_dir);
            cleanup_temp_dir(temp_dir);
        }
        *guard = None;
    }
}

/// Get the path to the temporary UI resources directory.
pub fn get_ui_temp_dir() -> Option<PathBuf> {
    UI_TEMP_DIR.lock().ok().and_then(|guard| guard.clone())
}

/// Check if custom UI resources are loaded.
pub fn has_custom_ui() -> bool {
    UI_TEMP_DIR.lock().ok().map(|guard| guard.is_some()).unwrap_or(false)
}

/// RAII guard for automatic cleanup of UI resources.
pub struct UIResourcesGuard;

impl UIResourcesGuard {
    /// Create a new guard that will clean up UI resources when dropped.
    pub fn new() -> Self {
        Self
    }
}

impl Drop for UIResourcesGuard {
    fn drop(&mut self) {
        cleanup_ui_resources();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn test_create_temp_dir() {
        let result = create_temp_dir();
        assert!(result.is_ok());
        
        let temp_dir = result.unwrap();
        assert!(temp_dir.exists());
        
        // Cleanup
        cleanup_temp_dir(&temp_dir);
        assert!(!temp_dir.exists());
    }

    #[test]
    fn test_cleanup_nonexistent_dir() {
        // Should not panic
        cleanup_temp_dir(Path::new("/nonexistent/path/12345"));
    }

    #[test]
    fn test_ui_resources_guard() {
        // Create a temp dir and store it
        let temp_dir = create_temp_dir().unwrap();
        if let Ok(mut guard) = UI_TEMP_DIR.lock() {
            *guard = Some(temp_dir.clone());
        }
        
        assert!(temp_dir.exists());
        
        // Create guard and drop it
        {
            let _guard = UIResourcesGuard::new();
        }
        
        // Temp dir should be cleaned up
        assert!(!temp_dir.exists());
    }
}
