use installer_core::{check_embedded_package, extract_embedded_package};
use std::path::PathBuf;
use tempfile::TempDir;
use tracing::{error, info, warn};

/// Extracted embedded package and its owning temp dir.
pub struct EmbeddedPackage {
    pub package_path: PathBuf,
    _temp_dir: TempDir,
}

/// Check current executable for embedded package data and extract it when present.
pub fn check_and_extract_embedded_package() -> Option<EmbeddedPackage> {
    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(e) => {
            warn!("Failed to get current executable path: {}", e);
            return None;
        }
    };

    match check_embedded_package(&exe_path) {
        Ok(Some(marker)) => {
            info!(
                "Found embedded package data at offset {}",
                marker.package_offset
            );

            let temp_dir = match tempfile::tempdir() {
                Ok(dir) => dir,
                Err(e) => {
                    error!("Failed to create temp directory: {}", e);
                    return None;
                }
            };

            let package_path = temp_dir.path().join("embedded.pkg");
            match extract_embedded_package(&exe_path, &package_path) {
                Ok(size) => {
                    info!("Extracted embedded package: {} bytes", size);
                    Some(EmbeddedPackage {
                        package_path,
                        _temp_dir: temp_dir,
                    })
                }
                Err(e) => {
                    error!("Failed to extract embedded package: {}", e);
                    None
                }
            }
        }
        Ok(None) => {
            info!("No embedded package data found - running as template");
            None
        }
        Err(e) => {
            warn!("Error checking for embedded package: {}", e);
            None
        }
    }
}

#[tauri::command]
pub fn get_embedded_package_path() -> Option<String> {
    std::env::var("INSTALLER_EMBEDDED_PACKAGE").ok()
}
