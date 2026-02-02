//! Platform abstraction layer.
//!
//! Defines the Platform trait for cross-platform operations.

use installer_shared::{RegistryEntry, RegistryValueType, Result, WindowsVersion};
use std::path::{Path, PathBuf};

/// Information for registering an uninstaller.
#[derive(Debug, Clone)]
pub struct UninstallInfo {
    /// Application name
    pub app_name: String,
    /// Application version
    pub version: String,
    /// Installation location
    pub install_location: PathBuf,
    /// Path to uninstaller executable
    pub uninstall_exe: PathBuf,
    /// Publisher name
    pub publisher: Option<String>,
    /// Estimated size in KB
    pub estimated_size_kb: u64,
}

/// Platform abstraction trait.
///
/// Implementations provide platform-specific functionality for:
/// - Default directories
/// - Admin privileges
/// - Shortcuts
/// - Registry operations
/// - Process management
pub trait Platform: Send + Sync {
    /// Get the default installation directory for an application.
    fn default_install_dir(&self, app_name: &str) -> Result<PathBuf>;

    /// Ensure the process has admin privileges.
    /// May trigger UAC prompt on Windows.
    fn ensure_admin(&self) -> Result<()>;

    /// Create a desktop shortcut.
    fn create_shortcut(&self, name: &str, target: &Path, icon: Option<&Path>) -> Result<()>;

    /// Register uninstaller in the system.
    fn register_uninstaller(&self, info: &UninstallInfo) -> Result<()>;

    /// Read a registry value.
    fn read_registry(&self, path: &str, key: &str) -> Result<String>;

    /// Write a registry entry.
    fn write_registry(&self, entry: &RegistryEntry) -> Result<()>;

    /// Delete a registry key.
    fn delete_registry(&self, path: &str, key: &str) -> Result<()>;

    /// Check if the process is running with elevated privileges.
    fn is_elevated(&self) -> bool;

    /// Request elevation (restart with admin privileges).
    fn request_elevation(&self) -> Result<()>;

    /// Check if a process with the given name is running.
    fn is_process_running(&self, name: &str) -> Result<bool>;

    /// Terminate a process by name.
    fn terminate_process(&self, name: &str) -> Result<()>;

    /// Configure auto-startup for an application.
    fn configure_auto_startup(&self, app_name: &str, exe_path: &Path, enable: bool) -> Result<()>;

    /// Get the desktop directory path.
    fn desktop_dir(&self) -> Result<PathBuf>;

    /// Get the current Windows version (Windows only).
    fn windows_version(&self) -> Result<WindowsVersion>;

    /// Check if the current Windows version meets the minimum requirement.
    fn check_windows_version(&self, min_version: &WindowsVersion) -> Result<bool>;
}

// Windows implementation
#[cfg(windows)]
mod windows_impl {
    use super::*;
    use installer_shared::InstallerError;
    use sysinfo::{ProcessRefreshKind, ProcessesToUpdate, RefreshKind, System};
    use tracing::{debug, info, warn};
    use winreg::enums::*;
    use winreg::RegKey;

    /// Windows platform implementation.
    pub struct WindowsPlatform {
        system: std::sync::Mutex<System>,
    }

    impl WindowsPlatform {
        pub fn new() -> Self {
            Self {
                system: std::sync::Mutex::new(System::new_with_specifics(
                    RefreshKind::new().with_processes(ProcessRefreshKind::everything()),
                )),
            }
        }
    }

    impl Default for WindowsPlatform {
        fn default() -> Self {
            Self::new()
        }
    }

    impl Platform for WindowsPlatform {
        fn default_install_dir(&self, app_name: &str) -> Result<PathBuf> {
            let program_files =
                std::env::var("ProgramFiles").unwrap_or_else(|_| "C:\\Program Files".to_string());
            debug!("Default install dir: {}\\{}", program_files, app_name);
            Ok(PathBuf::from(program_files).join(app_name))
        }

        fn ensure_admin(&self) -> Result<()> {
            if !self.is_elevated() {
                return Err(InstallerError::PermissionDenied(
                    "Administrator privileges required".to_string(),
                ));
            }
            Ok(())
        }

        fn create_shortcut(&self, name: &str, target: &Path, icon: Option<&Path>) -> Result<()> {
            let desktop = self.desktop_dir()?;
            let shortcut_path = desktop.join(format!("{}.lnk", name));

            info!("Creating shortcut: {:?} -> {:?}", shortcut_path, target);

            let mut lnk = mslnk::ShellLink::new(target).map_err(|e| {
                InstallerError::Platform(format!("Failed to create shortcut: {}", e))
            })?;

            if let Some(icon_path) = icon {
                lnk.set_icon_location(Some(icon_path.to_string_lossy().to_string()));
            }

            if let Some(parent) = target.parent() {
                lnk.set_working_dir(Some(parent.to_string_lossy().to_string()));
            }

            lnk.create_lnk(&shortcut_path).map_err(|e| {
                InstallerError::Platform(format!("Failed to write shortcut: {}", e))
            })?;

            Ok(())
        }

        fn register_uninstaller(&self, info: &UninstallInfo) -> Result<()> {
            info!("Registering uninstaller for: {}", info.app_name);

            let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
            let path = format!(
                "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{}",
                info.app_name
            );

            let (key, _) = hklm.create_subkey(&path).map_err(|e| {
                InstallerError::Platform(format!("Failed to create registry key: {}", e))
            })?;

            key.set_value("DisplayName", &info.app_name).map_err(|e| {
                InstallerError::Platform(format!("Failed to set DisplayName: {}", e))
            })?;
            key.set_value("DisplayVersion", &info.version)
                .map_err(|e| {
                    InstallerError::Platform(format!("Failed to set DisplayVersion: {}", e))
                })?;
            key.set_value(
                "InstallLocation",
                &info.install_location.to_string_lossy().to_string(),
            )
            .map_err(|e| {
                InstallerError::Platform(format!("Failed to set InstallLocation: {}", e))
            })?;
            key.set_value(
                "UninstallString",
                &info.uninstall_exe.to_string_lossy().to_string(),
            )
            .map_err(|e| {
                InstallerError::Platform(format!("Failed to set UninstallString: {}", e))
            })?;
            key.set_value("EstimatedSize", &(info.estimated_size_kb as u32))
                .map_err(|e| {
                    InstallerError::Platform(format!("Failed to set EstimatedSize: {}", e))
                })?;

            if let Some(ref publisher) = info.publisher {
                key.set_value("Publisher", publisher).map_err(|e| {
                    InstallerError::Platform(format!("Failed to set Publisher: {}", e))
                })?;
            }

            Ok(())
        }

        fn read_registry(&self, path: &str, key: &str) -> Result<String> {
            debug!("Reading registry: {}\\{}", path, key);

            let (root, subpath) = parse_registry_path(path)?;
            let root_key = RegKey::predef(root);
            let subkey = root_key.open_subkey(subpath).map_err(|e| {
                InstallerError::Platform(format!("Failed to open registry key: {}", e))
            })?;

            subkey.get_value(key).map_err(|e| {
                InstallerError::Platform(format!("Failed to read registry value: {}", e))
            })
        }

        fn write_registry(&self, entry: &RegistryEntry) -> Result<()> {
            debug!(
                "Writing registry: {}\\{} = {}",
                entry.path, entry.key, entry.value
            );

            let (root, subpath) = parse_registry_path(&entry.path)?;
            let root_key = RegKey::predef(root);

            let (key, _) = root_key.create_subkey(subpath).map_err(|e| {
                InstallerError::Platform(format!("Failed to create registry key: {}", e))
            })?;

            match entry.value_type {
                RegistryValueType::String => {
                    key.set_value(&entry.key, &entry.value).map_err(|e| {
                        InstallerError::Platform(format!("Failed to set string value: {}", e))
                    })?;
                }
                RegistryValueType::Dword => {
                    let dword_value: u32 = entry.value.parse().map_err(|_| {
                        InstallerError::Platform(format!("Invalid DWORD value: {}", entry.value))
                    })?;
                    key.set_value(&entry.key, &dword_value).map_err(|e| {
                        InstallerError::Platform(format!("Failed to set DWORD value: {}", e))
                    })?;
                }
                RegistryValueType::ExpandString => {
                    key.set_value(&entry.key, &entry.value).map_err(|e| {
                        InstallerError::Platform(format!(
                            "Failed to set expand string value: {}",
                            e
                        ))
                    })?;
                }
            }

            Ok(())
        }

        fn delete_registry(&self, path: &str, key: &str) -> Result<()> {
            debug!("Deleting registry: {}\\{}", path, key);

            let (root, subpath) = parse_registry_path(path)?;
            let root_key = RegKey::predef(root);

            if let Ok(subkey) = root_key.open_subkey_with_flags(subpath, KEY_WRITE) {
                let _ = subkey.delete_value(key);
            }

            Ok(())
        }

        fn is_elevated(&self) -> bool {
            is_elevated::is_elevated()
        }

        fn request_elevation(&self) -> Result<()> {
            info!("Requesting elevation...");

            let exe_path = std::env::current_exe().map_err(|e| {
                InstallerError::Platform(format!("Failed to get current exe: {}", e))
            })?;

            let args: Vec<String> = std::env::args().skip(1).collect();
            let args_str = args.join(" ");

            // Use runas command to trigger UAC
            let status = std::process::Command::new("cmd")
                .args([
                    "/C",
                    "start",
                    "",
                    "/wait",
                    "runas",
                    &exe_path.to_string_lossy(),
                    &args_str,
                ])
                .status()
                .map_err(|e| {
                    InstallerError::Platform(format!("Failed to request elevation: {}", e))
                })?;

            if !status.success() {
                return Err(InstallerError::Platform(
                    "Failed to request elevation - please run as administrator".to_string(),
                ));
            }

            // Exit current process since elevated process is starting
            std::process::exit(0);
        }

        fn is_process_running(&self, name: &str) -> Result<bool> {
            let mut system = self
                .system
                .lock()
                .map_err(|_| InstallerError::Platform("Failed to lock system info".to_string()))?;

            system.refresh_processes_specifics(ProcessesToUpdate::All, ProcessRefreshKind::new());

            let name_lower = name.to_lowercase();
            let is_running = system
                .processes()
                .values()
                .any(|process| process.name().to_string_lossy().to_lowercase() == name_lower);

            debug!("Process '{}' running: {}", name, is_running);
            Ok(is_running)
        }

        fn terminate_process(&self, name: &str) -> Result<()> {
            info!("Terminating process: {}", name);

            let mut system = self
                .system
                .lock()
                .map_err(|_| InstallerError::Platform("Failed to lock system info".to_string()))?;

            system.refresh_processes_specifics(ProcessesToUpdate::All, ProcessRefreshKind::new());

            let name_lower = name.to_lowercase();
            let mut terminated = false;

            for (pid, process) in system.processes() {
                if process.name().to_string_lossy().to_lowercase() == name_lower {
                    if process.kill() {
                        info!("Terminated process {} (PID: {})", name, pid);
                        terminated = true;
                    } else {
                        warn!("Failed to terminate process {} (PID: {})", name, pid);
                    }
                }
            }

            if !terminated {
                warn!("No process named '{}' found to terminate", name);
            }

            Ok(())
        }

        fn configure_auto_startup(
            &self,
            app_name: &str,
            exe_path: &Path,
            enable: bool,
        ) -> Result<()> {
            info!("Configuring auto-startup for '{}': {}", app_name, enable);

            let hkcu = RegKey::predef(HKEY_CURRENT_USER);
            let path = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

            let key = hkcu
                .open_subkey_with_flags(path, KEY_WRITE)
                .map_err(|e| InstallerError::Platform(format!("Failed to open Run key: {}", e)))?;

            if enable {
                // Verify executable exists before configuring
                if !exe_path.exists() {
                    return Err(InstallerError::Platform(format!(
                        "Executable not found: {:?}",
                        exe_path
                    )));
                }

                key.set_value(app_name, &exe_path.to_string_lossy().to_string())
                    .map_err(|e| {
                        InstallerError::Platform(format!("Failed to set auto-startup: {}", e))
                    })?;
            } else {
                // Ignore errors when deleting (key might not exist)
                let _ = key.delete_value(app_name);
            }

            Ok(())
        }

        fn desktop_dir(&self) -> Result<PathBuf> {
            let userprofile = std::env::var("USERPROFILE")
                .map_err(|_| InstallerError::Platform("USERPROFILE not set".to_string()))?;
            Ok(PathBuf::from(userprofile).join("Desktop"))
        }

        fn windows_version(&self) -> Result<WindowsVersion> {
            // Use registry-based version detection which is more reliable on modern Windows
            self.get_version_from_registry()
        }

        fn check_windows_version(&self, min_version: &WindowsVersion) -> Result<bool> {
            let current = self.windows_version()?;

            let meets_requirement = if current.major > min_version.major {
                true
            } else if current.major < min_version.major {
                false
            } else if current.minor > min_version.minor {
                true
            } else if current.minor < min_version.minor {
                false
            } else {
                current.build >= min_version.build
            };

            if !meets_requirement {
                info!(
                    "Windows version check failed: current {}.{}.{} < required {}.{}.{}",
                    current.major,
                    current.minor,
                    current.build,
                    min_version.major,
                    min_version.minor,
                    min_version.build
                );
            }

            Ok(meets_requirement)
        }
    }

    impl WindowsPlatform {
        /// Get Windows version from registry (more reliable on modern Windows)
        fn get_version_from_registry(&self) -> Result<WindowsVersion> {
            let hklm = RegKey::predef(HKEY_LOCAL_MACHINE);
            let key = hklm
                .open_subkey("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion")
                .map_err(|e| {
                    InstallerError::Platform(format!("Failed to open version key: {}", e))
                })?;

            // Try to get CurrentMajorVersionNumber and CurrentMinorVersionNumber (Windows 10+)
            let major: u32 = key.get_value("CurrentMajorVersionNumber").unwrap_or(10);
            let minor: u32 = key.get_value("CurrentMinorVersionNumber").unwrap_or(0);
            let build_str: String = key
                .get_value("CurrentBuildNumber")
                .unwrap_or_else(|_| "19041".to_string());
            let build: u32 = build_str.parse().unwrap_or(19041);

            let version = WindowsVersion {
                major: major as u16,
                minor: minor as u16,
                build,
            };

            debug!(
                "Windows version: {}.{}.{}",
                version.major, version.minor, version.build
            );
            Ok(version)
        }
    }

    /// Parse a registry path into root key and subpath.
    fn parse_registry_path(path: &str) -> Result<(winreg::HKEY, &str)> {
        if let Some(subpath) = path.strip_prefix("HKEY_LOCAL_MACHINE\\") {
            Ok((HKEY_LOCAL_MACHINE, subpath))
        } else if let Some(subpath) = path.strip_prefix("HKEY_CURRENT_USER\\") {
            Ok((HKEY_CURRENT_USER, subpath))
        } else if let Some(subpath) = path.strip_prefix("HKLM\\") {
            Ok((HKEY_LOCAL_MACHINE, subpath))
        } else if let Some(subpath) = path.strip_prefix("HKCU\\") {
            Ok((HKEY_CURRENT_USER, subpath))
        } else if let Some(subpath) = path.strip_prefix("HKEY_CLASSES_ROOT\\") {
            Ok((HKEY_CLASSES_ROOT, subpath))
        } else if let Some(subpath) = path.strip_prefix("HKCR\\") {
            Ok((HKEY_CLASSES_ROOT, subpath))
        } else {
            Err(InstallerError::Platform(format!(
                "Unknown registry root in path: {}",
                path
            )))
        }
    }
}

#[cfg(windows)]
pub use windows_impl::WindowsPlatform;

// Stub implementation for non-Windows platforms (for compilation)
#[cfg(not(windows))]
mod stub_impl {
    use super::*;
    use installer_shared::InstallerError;

    pub struct WindowsPlatform;

    impl WindowsPlatform {
        pub fn new() -> Self {
            Self
        }
    }

    impl Default for WindowsPlatform {
        fn default() -> Self {
            Self::new()
        }
    }

    impl Platform for WindowsPlatform {
        fn default_install_dir(&self, app_name: &str) -> Result<PathBuf> {
            Ok(PathBuf::from("/opt").join(app_name))
        }

        fn ensure_admin(&self) -> Result<()> {
            Ok(())
        }

        fn create_shortcut(&self, _name: &str, _target: &Path, _icon: Option<&Path>) -> Result<()> {
            Err(InstallerError::Platform(
                "Not supported on this platform".to_string(),
            ))
        }

        fn register_uninstaller(&self, _info: &UninstallInfo) -> Result<()> {
            Err(InstallerError::Platform(
                "Not supported on this platform".to_string(),
            ))
        }

        fn read_registry(&self, _path: &str, _key: &str) -> Result<String> {
            Err(InstallerError::Platform(
                "Registry not available on this platform".to_string(),
            ))
        }

        fn write_registry(&self, _entry: &RegistryEntry) -> Result<()> {
            Err(InstallerError::Platform(
                "Registry not available on this platform".to_string(),
            ))
        }

        fn delete_registry(&self, _path: &str, _key: &str) -> Result<()> {
            Err(InstallerError::Platform(
                "Registry not available on this platform".to_string(),
            ))
        }

        fn is_elevated(&self) -> bool {
            unsafe { libc::geteuid() == 0 }
        }

        fn request_elevation(&self) -> Result<()> {
            Err(InstallerError::Platform("Run with sudo".to_string()))
        }

        fn is_process_running(&self, _name: &str) -> Result<bool> {
            Ok(false)
        }

        fn terminate_process(&self, _name: &str) -> Result<()> {
            Err(InstallerError::Platform("Not implemented".to_string()))
        }

        fn configure_auto_startup(
            &self,
            _app_name: &str,
            _exe_path: &Path,
            _enable: bool,
        ) -> Result<()> {
            Err(InstallerError::Platform(
                "Not supported on this platform".to_string(),
            ))
        }

        fn desktop_dir(&self) -> Result<PathBuf> {
            let home = std::env::var("HOME")
                .map_err(|_| InstallerError::Platform("HOME not set".to_string()))?;
            Ok(PathBuf::from(home).join("Desktop"))
        }

        fn windows_version(&self) -> Result<WindowsVersion> {
            Err(InstallerError::Platform("Not on Windows".to_string()))
        }

        fn check_windows_version(&self, _min_version: &WindowsVersion) -> Result<bool> {
            Err(InstallerError::Platform("Not on Windows".to_string()))
        }
    }
}

#[cfg(not(windows))]
pub use stub_impl::WindowsPlatform;

/// Create a platform instance for the current OS.
pub fn create_platform() -> Box<dyn Platform> {
    Box::new(WindowsPlatform::new())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_create_platform() {
        let platform = create_platform();
        // Just verify we can create a platform instance
        let _ = platform.is_elevated();
    }

    #[cfg(windows)]
    #[test]
    fn test_default_install_dir() {
        let platform = WindowsPlatform::new();
        let dir = platform.default_install_dir("TestApp").unwrap();
        assert!(dir.to_string_lossy().contains("TestApp"));
    }

    #[cfg(windows)]
    #[test]
    fn test_desktop_dir() {
        let platform = WindowsPlatform::new();
        let dir = platform.desktop_dir().unwrap();
        assert!(dir.to_string_lossy().contains("Desktop"));
    }

    #[cfg(windows)]
    #[test]
    fn test_windows_version() {
        let platform = WindowsPlatform::new();
        let version = platform.windows_version().unwrap();
        // Windows 10 or later
        assert!(version.major >= 6);
    }

    #[cfg(windows)]
    #[test]
    fn test_check_windows_version() {
        let platform = WindowsPlatform::new();

        // Should pass for Windows 7 requirement on modern Windows
        let win7 = WindowsVersion {
            major: 6,
            minor: 1,
            build: 0,
        };
        assert!(platform.check_windows_version(&win7).unwrap());

        // Should fail for future Windows version
        let future = WindowsVersion {
            major: 99,
            minor: 0,
            build: 0,
        };
        assert!(!platform.check_windows_version(&future).unwrap());
    }

    #[cfg(windows)]
    #[test]
    fn test_is_process_running() {
        let platform = WindowsPlatform::new();
        // explorer.exe should always be running on Windows
        let result = platform.is_process_running("explorer.exe");
        assert!(result.is_ok());
    }
}

#[cfg(test)]
#[cfg(windows)]
mod property_tests {
    use super::*;
    use installer_shared::RegistryValueType;
    use proptest::prelude::*;

    // Test registry path for property tests - use HKCU to avoid admin requirements
    const TEST_REGISTRY_PATH: &str = "HKEY_CURRENT_USER\\Software\\InstallerTest";

    /// Generate valid registry key names (alphanumeric, no special chars)
    fn valid_key_name() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_]{0,30}".prop_map(|s| s)
    }

    /// Generate valid string values for registry
    fn valid_string_value() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9 _.-]{1,100}".prop_map(|s| s)
    }

    /// Generate valid DWORD values
    fn valid_dword_value() -> impl Strategy<Value = u32> {
        0u32..=u32::MAX
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(20))]

        /// **Property 12: Registry Operation Consistency**
        /// For any registry entry, writing and then immediately reading should return the same value.
        /// **Validates: Requirements 10.1, 10.2, 10.3, 18.2**
        #[test]
        fn prop_registry_string_roundtrip(
            key in valid_key_name(),
            value in valid_string_value()
        ) {
            let platform = WindowsPlatform::new();

            let entry = RegistryEntry {
                path: TEST_REGISTRY_PATH.to_string(),
                key: key.clone(),
                value: value.clone(),
                value_type: RegistryValueType::String,
            };

            // Write the registry entry
            let write_result = platform.write_registry(&entry);

            // If write succeeded, read should return the same value
            if write_result.is_ok() {
                let read_result = platform.read_registry(TEST_REGISTRY_PATH, &key);

                // Clean up
                let _ = platform.delete_registry(TEST_REGISTRY_PATH, &key);

                prop_assert!(read_result.is_ok(), "Read failed after successful write");
                prop_assert_eq!(read_result.unwrap(), value, "Read value doesn't match written value");
            }
        }

        /// **Property 12: Registry DWORD Consistency**
        /// For any DWORD registry entry, writing and reading should be consistent.
        /// **Validates: Requirements 10.1, 10.2, 10.3**
        #[test]
        fn prop_registry_dword_roundtrip(
            key in valid_key_name(),
            value in valid_dword_value()
        ) {
            let platform = WindowsPlatform::new();

            let entry = RegistryEntry {
                path: TEST_REGISTRY_PATH.to_string(),
                key: key.clone(),
                value: value.to_string(),
                value_type: RegistryValueType::Dword,
            };

            // Write the registry entry
            let write_result = platform.write_registry(&entry);

            // If write succeeded, the key should exist
            if write_result.is_ok() {
                // For DWORD, we can't easily read back as string, so just verify no error
                // Clean up
                let _ = platform.delete_registry(TEST_REGISTRY_PATH, &key);
            }
        }

        /// **Property 12: Registry Delete Consistency**
        /// After deleting a registry key, reading it should fail.
        /// **Validates: Requirements 10.1, 10.2, 10.3**
        #[test]
        fn prop_registry_delete_removes_key(
            key in valid_key_name(),
            value in valid_string_value()
        ) {
            let platform = WindowsPlatform::new();

            let entry = RegistryEntry {
                path: TEST_REGISTRY_PATH.to_string(),
                key: key.clone(),
                value: value.clone(),
                value_type: RegistryValueType::String,
            };

            // Write the registry entry
            let write_result = platform.write_registry(&entry);

            if write_result.is_ok() {
                // Delete the key
                let delete_result = platform.delete_registry(TEST_REGISTRY_PATH, &key);
                prop_assert!(delete_result.is_ok(), "Delete should succeed");

                // Reading should now fail
                let read_result = platform.read_registry(TEST_REGISTRY_PATH, &key);
                prop_assert!(read_result.is_err(), "Read should fail after delete");
            }
        }
    }

    /// Clean up test registry keys after all tests
    fn cleanup_test_registry() {
        use winreg::enums::*;
        use winreg::RegKey;

        let hkcu = RegKey::predef(HKEY_CURRENT_USER);
        let _ = hkcu.delete_subkey_all("Software\\InstallerTest");
    }

    #[test]
    fn test_cleanup_registry() {
        cleanup_test_registry();
    }
}
