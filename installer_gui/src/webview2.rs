//! WebView2 runtime detection and installation guidance.
//!
//! This module provides functionality to:
//! - Detect if WebView2 runtime is installed
//! - Display installation prompts to users
//! - Provide fallback to CLI mode
//!
//! # Requirements
//! - 17.1: Check WebView2 runtime on Windows startup
//! - 17.2: Use WebView2 loader API for runtime detection
//! - 17.3: Display installation prompt if not found
//! - 17.4: Provide download link for WebView2 runtime
//! - 17.5: Provide option to continue without GUI (CLI mode)

use tracing::{debug, info, warn};

/// WebView2 download URL
pub const WEBVIEW2_DOWNLOAD_URL: &str = 
    "https://developer.microsoft.com/en-us/microsoft-edge/webview2/#download-section";

/// WebView2 bootstrapper download URL (direct)
pub const WEBVIEW2_BOOTSTRAPPER_URL: &str = 
    "https://go.microsoft.com/fwlink/p/?LinkId=2124703";

/// Result of WebView2 detection
#[derive(Debug, Clone)]
pub struct WebView2Status {
    /// Whether WebView2 is installed
    pub is_installed: bool,
    /// Version string if installed
    pub version: Option<String>,
    /// Error message if detection failed
    pub error: Option<String>,
}

impl WebView2Status {
    /// Create a status indicating WebView2 is installed
    pub fn installed(version: String) -> Self {
        Self {
            is_installed: true,
            version: Some(version),
            error: None,
        }
    }

    /// Create a status indicating WebView2 is not installed
    pub fn not_installed() -> Self {
        Self {
            is_installed: false,
            version: None,
            error: None,
        }
    }

    /// Create a status indicating detection failed
    pub fn detection_failed(error: String) -> Self {
        Self {
            is_installed: false,
            version: None,
            error: Some(error),
        }
    }
}

/// Check if WebView2 runtime is installed on the system.
///
/// On Windows, this checks the registry for WebView2 installation.
/// On other platforms, this always returns true (WebView2 is Windows-only).
///
/// # Returns
/// * `WebView2Status` with installation status and version info
///
/// # Requirements
/// - 17.1: Check WebView2 runtime on Windows startup
/// - 17.2: Use WebView2 loader API for runtime detection
pub fn check_webview2() -> WebView2Status {
    #[cfg(windows)]
    {
        check_webview2_windows()
    }
    
    #[cfg(not(windows))]
    {
        // On non-Windows platforms, assume WebView is available
        // (Tauri uses different WebView implementations)
        WebView2Status::installed("native".to_string())
    }
}

/// Windows-specific WebView2 detection
#[cfg(windows)]
fn check_webview2_windows() -> WebView2Status {
    use winreg::enums::*;
    use winreg::RegKey;

    debug!("Checking for WebView2 runtime installation");

    // Check multiple registry locations where WebView2 might be registered
    let registry_paths = [
        // Per-machine installation (64-bit)
        (HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
        // Per-machine installation (32-bit)
        (HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
        // Per-user installation
        (HKEY_CURRENT_USER, r"SOFTWARE\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"),
    ];

    for (hkey, path) in registry_paths {
        match RegKey::predef(hkey).open_subkey(path) {
            Ok(key) => {
                match key.get_value::<String, _>("pv") {
                    Ok(version) => {
                        if !version.is_empty() && version != "0.0.0.0" {
                            info!("WebView2 found: version {}", version);
                            return WebView2Status::installed(version);
                        }
                    }
                    Err(e) => {
                        debug!("Could not read version from {}: {}", path, e);
                    }
                }
            }
            Err(e) => {
                debug!("Registry path not found {}: {}", path, e);
            }
        }
    }

    // Also check for Edge WebView2 Runtime in Program Files
    let program_files_paths = [
        r"C:\Program Files (x86)\Microsoft\EdgeWebView\Application",
        r"C:\Program Files\Microsoft\EdgeWebView\Application",
    ];

    for path in program_files_paths {
        let path = std::path::Path::new(path);
        if path.exists() {
            // Look for version folders
            if let Ok(entries) = std::fs::read_dir(path) {
                for entry in entries.flatten() {
                    let name = entry.file_name();
                    let name_str = name.to_string_lossy();
                    // Version folders look like "120.0.2210.91"
                    if name_str.chars().next().map(|c| c.is_ascii_digit()).unwrap_or(false) {
                        info!("WebView2 found in Program Files: version {}", name_str);
                        return WebView2Status::installed(name_str.to_string());
                    }
                }
            }
        }
    }

    warn!("WebView2 runtime not found");
    WebView2Status::not_installed()
}

/// Get the WebView2 download URL for user guidance.
pub fn get_download_url() -> &'static str {
    WEBVIEW2_DOWNLOAD_URL
}

/// Get the WebView2 bootstrapper URL for automatic download.
pub fn get_bootstrapper_url() -> &'static str {
    WEBVIEW2_BOOTSTRAPPER_URL
}

/// Open the WebView2 download page in the default browser.
///
/// # Requirements
/// - 17.6: Open download URL in browser when user chooses to install
pub fn open_download_page() -> Result<(), String> {
    #[cfg(windows)]
    {
        use std::process::Command;
        
        Command::new("cmd")
            .args(["/C", "start", "", WEBVIEW2_DOWNLOAD_URL])
            .spawn()
            .map_err(|e| format!("Failed to open browser: {}", e))?;
        
        info!("Opened WebView2 download page in browser");
        Ok(())
    }
    
    #[cfg(not(windows))]
    {
        Err("WebView2 is only available on Windows".to_string())
    }
}

/// Display options for the user when WebView2 is not installed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WebView2Action {
    /// Install WebView2 (opens download page)
    Install,
    /// Continue with CLI mode
    UseCli,
    /// Cancel and exit
    Cancel,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_webview2_status_installed() {
        let status = WebView2Status::installed("120.0.2210.91".to_string());
        assert!(status.is_installed);
        assert_eq!(status.version, Some("120.0.2210.91".to_string()));
        assert!(status.error.is_none());
    }

    #[test]
    fn test_webview2_status_not_installed() {
        let status = WebView2Status::not_installed();
        assert!(!status.is_installed);
        assert!(status.version.is_none());
        assert!(status.error.is_none());
    }

    #[test]
    fn test_webview2_status_detection_failed() {
        let status = WebView2Status::detection_failed("Registry access denied".to_string());
        assert!(!status.is_installed);
        assert!(status.version.is_none());
        assert_eq!(status.error, Some("Registry access denied".to_string()));
    }

    #[test]
    fn test_download_urls() {
        assert!(!get_download_url().is_empty());
        assert!(get_download_url().starts_with("https://"));
        assert!(!get_bootstrapper_url().is_empty());
        assert!(get_bootstrapper_url().starts_with("https://"));
    }

    #[test]
    fn test_check_webview2() {
        // This test just ensures the function doesn't panic
        let status = check_webview2();
        // On non-Windows, it should always return installed
        #[cfg(not(windows))]
        assert!(status.is_installed);
    }
}
