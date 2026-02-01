//! Tauri commands for the installer GUI.
//!
//! This module provides the bridge between the frontend UI and the installer core.
//!
//! # Requirements
//! - 4.9: Implement Tauri commands for installation control
//! - 4.10: Implement Tauri commands for metadata queries

use crate::webview2;
use installer_core::{Installer, LocalizationManager};
use installer_shared::{InstallOptions, LocalizationConfig, PackageMetadata, Phase};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, Manager};
use tracing::{debug, error, info, warn};

/// Global installation state
lazy_static::lazy_static! {
    static ref INSTALL_STATE: Mutex<InstallState> = Mutex::new(InstallState::default());
    static ref LOCALIZATION: Mutex<Option<LocalizationManager>> = Mutex::new(None);
}

/// Installation state shared across commands.
pub struct InstallState {
    /// Flag to signal cancellation
    pub cancelled: Arc<AtomicBool>,
    /// Current package path
    pub package_path: Option<PathBuf>,
    /// Current locale
    pub current_locale: String,
}

impl Default for InstallState {
    fn default() -> Self {
        Self {
            cancelled: Arc::new(AtomicBool::new(false)),
            package_path: None,
            current_locale: "en-US".to_string(),
        }
    }
}

/// Response for metadata query.
#[derive(Debug, Serialize)]
pub struct MetadataResponse {
    pub app_name: String,
    pub version: String,
    pub default_install_dir: String,
    pub vendor: Option<String>,
    pub license_text: Option<String>,
    pub require_admin: bool,
    pub desktop_icons: bool,
    pub auto_startup: bool,
}

impl From<PackageMetadata> for MetadataResponse {
    fn from(m: PackageMetadata) -> Self {
        // Expand environment variables in the install directory path
        let expanded_install_dir = expand_env_vars(&m.default_install_dir);
        
        Self {
            app_name: m.app_name.clone(),
            version: m.version,
            default_install_dir: if expanded_install_dir.is_empty() {
                // Fallback to a sensible default
                format!("C:\\Program Files\\{}", m.app_name)
            } else {
                expanded_install_dir
            },
            vendor: m.vendor,
            license_text: m.license_text,
            require_admin: m.require_admin,
            desktop_icons: m.desktop_icons,
            auto_startup: m.auto_startup,
        }
    }
}

/// Expand environment variables in a path string.
/// Supports %VAR% syntax on Windows.
fn expand_env_vars(path: &str) -> String {
    let mut result = path.to_string();
    
    // Common Windows environment variables to expand
    let env_vars = [
        ("ProgramFiles", "PROGRAMFILES"),
        ("ProgramFiles(x86)", "PROGRAMFILES(X86)"),
        ("LocalAppData", "LOCALAPPDATA"),
        ("AppData", "APPDATA"),
        ("UserProfile", "USERPROFILE"),
        ("SystemRoot", "SYSTEMROOT"),
        ("WinDir", "WINDIR"),
        ("Temp", "TEMP"),
        ("Tmp", "TMP"),
    ];
    
    for (name, env_name) in env_vars {
        // Try both %Name% and %NAME% patterns
        let patterns = [
            format!("%{}%", name),
            format!("%{}%", name.to_uppercase()),
            format!("%{}%", env_name),
        ];
        
        for pattern in patterns {
            if result.contains(&pattern) {
                if let Ok(value) = std::env::var(env_name) {
                    result = result.replace(&pattern, &value);
                } else if let Ok(value) = std::env::var(name) {
                    result = result.replace(&pattern, &value);
                }
            }
        }
    }
    
    result
}

/// Get package metadata.
///
/// # Arguments
/// * `package_path` - Path to the installer package
///
/// # Returns
/// * `MetadataResponse` with package information
///
/// # Requirements
/// - 4.9: query_metadata command
#[tauri::command]
pub async fn get_metadata(package_path: String) -> Result<MetadataResponse, String> {
    info!("Getting metadata from: {}", package_path);
    
    let installer = Installer::new(PathBuf::from(&package_path))
        .map_err(|e| e.to_string())?;
    
    let parsed = installer.parse_package()
        .map_err(|e| e.to_string())?;
    
    // Store package path in state
    if let Ok(mut state) = INSTALL_STATE.lock() {
        state.package_path = Some(PathBuf::from(&package_path));
    }
    
    Ok(parsed.metadata.into())
}

/// Installation options from frontend.
#[derive(Debug, Deserialize)]
pub struct InstallRequest {
    pub package_path: String,
    pub install_dir: String,
    pub create_shortcuts: bool,
    pub auto_startup: bool,
}

/// Start installation.
///
/// # Arguments
/// * `app` - Tauri app handle for emitting events
/// * `request` - Installation request with options
///
/// # Requirements
/// - 4.9: start_install command
#[tauri::command]
pub async fn start_install(
    app: AppHandle,
    request: InstallRequest,
) -> Result<(), String> {
    info!("Starting installation: {:?}", request.install_dir);
    
    // Reset cancellation flag
    if let Ok(state) = INSTALL_STATE.lock() {
        state.cancelled.store(false, Ordering::SeqCst);
    }
    
    let installer = Installer::new(PathBuf::from(&request.package_path))
        .map_err(|e| e.to_string())?;
    
    let options = InstallOptions {
        install_dir: PathBuf::from(&request.install_dir),
        create_shortcuts: request.create_shortcuts,
        configure_registry: true,
        auto_startup: request.auto_startup,
        silent: false,
        thread_count: None,
    };
    
    // Get cancellation flag
    let cancelled = INSTALL_STATE.lock()
        .map(|s| s.cancelled.clone())
        .unwrap_or_else(|_| Arc::new(AtomicBool::new(false)));
    
    // Run installation in a blocking task
    let app_clone = app.clone();
    let cancelled_clone = cancelled.clone();
    
    // Track last emit time to throttle events and highest percentage to prevent regression
    let last_emit = Arc::new(std::sync::Mutex::new(std::time::Instant::now()));
    let highest_percentage = Arc::new(std::sync::Mutex::new(0.0f64));
    
    let result = tokio::task::spawn_blocking(move || {
        installer.install(options, |event| {
            // Check for cancellation
            if cancelled_clone.load(Ordering::SeqCst) {
                return;
            }
            
            // Calculate overall percentage based on phase
            // Phase weights: Decompressing 40%, Writing 55%, Completing 5%
            let phase_percentage = event.percentage();
            let overall_percentage = match event.phase {
                Phase::Decompressing => phase_percentage * 0.40,
                Phase::Writing => 40.0 + phase_percentage * 0.55,
                Phase::Completing => 95.0 + phase_percentage * 0.05,
                _ => phase_percentage,
            };
            
            // Check if we should emit (throttle + monotonic increase)
            let should_emit = {
                let mut last = last_emit.lock().unwrap();
                let mut highest = highest_percentage.lock().unwrap();
                let now = std::time::Instant::now();
                
                // Only emit if percentage increased and enough time has passed
                let time_ok = now.duration_since(*last).as_millis() >= 50;
                let is_final = event.current == event.total;
                let percentage_increased = overall_percentage > *highest;
                
                if (time_ok || is_final) && percentage_increased {
                    *last = now;
                    *highest = overall_percentage;
                    true
                } else {
                    false
                }
            };
            
            if should_emit {
                // Create a modified event with the overall percentage
                let modified_event = serde_json::json!({
                    "phase": format!("{:?}", event.phase),
                    "current": event.current,
                    "total": event.total,
                    "current_file": event.current_file,
                    "overall_percentage": overall_percentage,
                });
                
                debug!("Emitting progress event: phase={:?}, overall={:.1}%", 
                    event.phase, overall_percentage);
                
                // Emit progress event to frontend
                if let Err(e) = app_clone.emit("install_progress", &modified_event) {
                    warn!("Failed to emit progress event: {}", e);
                }
            }
        })
    })
    .await
    .map_err(|e| e.to_string())?;
    
    match result {
        Ok(stats) => {
            info!("Installation completed successfully");
            let _ = app.emit("install_complete", serde_json::json!({
                "files": stats.installed_files,
                "size": stats.total_size,
                "time_ms": stats.elapsed_time.as_millis(),
            }));
            Ok(())
        }
        Err(e) => {
            error!("Installation failed: {}", e);
            let _ = app.emit("install_error", e.to_string());
            Err(e.to_string())
        }
    }
}

/// Cancel ongoing installation.
///
/// # Requirements
/// - 4.9: cancel_install command
#[tauri::command]
pub async fn cancel_install() -> Result<(), String> {
    info!("Installation cancelled by user");
    
    if let Ok(state) = INSTALL_STATE.lock() {
        state.cancelled.store(true, Ordering::SeqCst);
    }
    
    Ok(())
}

/// Get system locale.
///
/// # Returns
/// * System locale string (e.g., "en-US", "zh-CN")
///
/// # Requirements
/// - 4.9: get_system_locale command
#[tauri::command]
pub async fn get_system_locale() -> String {
    let locale = LocalizationManager::detect_system_locale();
    info!("Detected system locale: {}", locale);
    locale
}

/// Prerequisites check result.
#[derive(Debug, Serialize)]
pub struct PrerequisitesResult {
    pub disk_space_ok: bool,
    pub version_ok: bool,
    pub process_running: bool,
    pub admin_required: bool,
    pub is_admin: bool,
    pub available_space_mb: u64,
    pub required_space_mb: u64,
    pub error_message: Option<String>,
}

/// Check installation prerequisites.
///
/// # Arguments
/// * `package_path` - Path to the installer package
/// * `install_dir` - Target installation directory
///
/// # Returns
/// * `PrerequisitesResult` with check results
#[tauri::command]
pub async fn check_prerequisites(
    package_path: String,
    install_dir: String,
) -> Result<PrerequisitesResult, String> {
    debug!("Checking prerequisites for installation to: {}", install_dir);
    
    let installer = Installer::new(PathBuf::from(&package_path))
        .map_err(|e| e.to_string())?;
    
    let parsed = installer.parse_package()
        .map_err(|e| e.to_string())?;
    
    // Check disk space
    let disk_space_result = installer.check_disk_space(&PathBuf::from(&install_dir));
    let disk_space_ok = disk_space_result.is_ok();
    let disk_error = disk_space_result.err().map(|e| e.to_string());
    
    // Check Windows version
    let version_ok = installer.check_windows_version().is_ok();
    
    // Check if process is running
    let process_running = installer.check_running_process().unwrap_or(false);
    
    // Calculate space requirements
    let required_bytes: u64 = parsed.toc.files.iter().map(|f| f.original_size).sum();
    let required_space_mb = required_bytes / (1024 * 1024);
    
    // Get available space
    let available_space_mb = installer_core::get_available_space(&PathBuf::from(&install_dir))
        .map(|bytes| bytes / (1024 * 1024))
        .unwrap_or(0);
    
    // Check admin status
    let is_admin = installer_core::platform::create_platform().is_elevated();
    
    Ok(PrerequisitesResult {
        disk_space_ok,
        version_ok,
        process_running,
        admin_required: parsed.metadata.require_admin,
        is_admin,
        available_space_mb,
        required_space_mb,
        error_message: disk_error,
    })
}

/// WebView2 status response.
#[derive(Debug, Serialize)]
pub struct WebView2StatusResponse {
    pub is_installed: bool,
    pub version: Option<String>,
    pub download_url: String,
}

/// Check WebView2 runtime status.
///
/// # Returns
/// * `WebView2StatusResponse` with installation status
#[tauri::command]
pub async fn check_webview2_status() -> WebView2StatusResponse {
    let status = webview2::check_webview2();
    
    WebView2StatusResponse {
        is_installed: status.is_installed,
        version: status.version,
        download_url: webview2::WEBVIEW2_DOWNLOAD_URL.to_string(),
    }
}

/// Browse for a directory using the system file dialog.
///
/// # Arguments
/// * `default_path` - Default path to start browsing from
///
/// # Returns
/// * Selected directory path or None if cancelled
#[tauri::command]
pub async fn browse_directory(
    _app: AppHandle,
    default_path: Option<String>,
) -> Result<Option<String>, String> {
    info!("Browse directory requested, default: {:?}", default_path);
    
    // Use rfd (Rust File Dialog) for cross-platform folder picker
    let result = tokio::task::spawn_blocking(move || {
        let mut dialog = rfd::FileDialog::new();
        
        // Set starting directory if provided
        if let Some(ref path) = default_path {
            let path_buf = std::path::PathBuf::from(path);
            if path_buf.exists() {
                dialog = dialog.set_directory(&path_buf);
            }
        }
        
        dialog.pick_folder().map(|p| p.to_string_lossy().to_string())
    })
    .await
    .map_err(|e| e.to_string())?;
    
    info!("Browse result: {:?}", result);
    Ok(result)
}

/// Translation response.
#[derive(Debug, Serialize)]
pub struct TranslationsResponse {
    pub locale: String,
    pub translations: HashMap<String, String>,
    pub supported_locales: Vec<String>,
}

/// Get translations for the current or specified locale.
///
/// # Arguments
/// * `locale` - Optional locale to get translations for (uses system locale if not specified)
/// * `ui_resources_path` - Optional path to UI resources directory
///
/// # Returns
/// * `TranslationsResponse` with translations for the locale
#[tauri::command]
pub async fn get_translations(
    locale: Option<String>,
    ui_resources_path: Option<String>,
) -> Result<TranslationsResponse, String> {
    let target_locale = locale.unwrap_or_else(|| LocalizationManager::detect_system_locale());
    
    debug!("Getting translations for locale: {}", target_locale);
    
    // Try to load from UI resources if path provided
    if let Some(path) = ui_resources_path {
        let resources_path = PathBuf::from(&path);
        if resources_path.exists() {
            let config = LocalizationConfig {
                default_locale: "en-US".to_string(),
                fallback_locale: "en-US".to_string(),
                supported_locales: vec![
                    "en-US".to_string(),
                    "zh-CN".to_string(),
                ],
            };
            
            let mut manager = LocalizationManager::new(config.clone());
            if let Err(e) = manager.load_from_resources(&resources_path) {
                warn!("Failed to load translations from resources: {}", e);
            } else {
                // Set the locale
                let _ = manager.set_locale(&target_locale);
                
                // Get all translations
                let mut translations = HashMap::new();
                for key in manager.all_keys() {
                    translations.insert(key.clone(), manager.get_text(key));
                }
                
                return Ok(TranslationsResponse {
                    locale: target_locale,
                    translations,
                    supported_locales: config.supported_locales,
                });
            }
        }
    }
    
    // Return default translations
    let default_translations = get_default_translations(&target_locale);
    
    Ok(TranslationsResponse {
        locale: target_locale,
        translations: default_translations,
        supported_locales: vec!["en-US".to_string(), "zh-CN".to_string()],
    })
}

/// Get default built-in translations.
fn get_default_translations(locale: &str) -> HashMap<String, String> {
    let mut translations = HashMap::new();
    
    if locale.starts_with("zh") {
        // Chinese translations
        translations.insert("welcome.title".to_string(), "欢迎".to_string());
        translations.insert("welcome.description".to_string(), "这将在您的计算机上安装 {appName}。".to_string());
        translations.insert("install.directory".to_string(), "安装目录".to_string());
        translations.insert("install.progress".to_string(), "正在安装...".to_string());
        translations.insert("install.complete".to_string(), "安装完成".to_string());
        translations.insert("install.success".to_string(), "应用程序已成功安装。".to_string());
        translations.insert("error.disk_space".to_string(), "磁盘空间不足".to_string());
        translations.insert("error.version".to_string(), "不支持的 Windows 版本".to_string());
        translations.insert("error.process_running".to_string(), "请在安装前关闭应用程序".to_string());
        translations.insert("option.shortcuts".to_string(), "创建桌面快捷方式".to_string());
        translations.insert("option.startup".to_string(), "开机自动启动".to_string());
        translations.insert("option.launch".to_string(), "启动应用程序".to_string());
        translations.insert("button.next".to_string(), "下一步".to_string());
        translations.insert("button.back".to_string(), "上一步".to_string());
        translations.insert("button.install".to_string(), "安装".to_string());
        translations.insert("button.cancel".to_string(), "取消".to_string());
        translations.insert("button.finish".to_string(), "完成".to_string());
        translations.insert("button.browse".to_string(), "浏览".to_string());
    } else {
        // English translations (default)
        translations.insert("welcome.title".to_string(), "Welcome".to_string());
        translations.insert("welcome.description".to_string(), "This will install {appName} on your computer.".to_string());
        translations.insert("install.directory".to_string(), "Installation Directory".to_string());
        translations.insert("install.progress".to_string(), "Installing...".to_string());
        translations.insert("install.complete".to_string(), "Installation Complete".to_string());
        translations.insert("install.success".to_string(), "The application has been installed successfully.".to_string());
        translations.insert("error.disk_space".to_string(), "Insufficient disk space".to_string());
        translations.insert("error.version".to_string(), "Windows version not supported".to_string());
        translations.insert("error.process_running".to_string(), "Please close the application before installing".to_string());
        translations.insert("option.shortcuts".to_string(), "Create desktop shortcut".to_string());
        translations.insert("option.startup".to_string(), "Start with Windows".to_string());
        translations.insert("option.launch".to_string(), "Launch application".to_string());
        translations.insert("button.next".to_string(), "Next".to_string());
        translations.insert("button.back".to_string(), "Back".to_string());
        translations.insert("button.install".to_string(), "Install".to_string());
        translations.insert("button.cancel".to_string(), "Cancel".to_string());
        translations.insert("button.finish".to_string(), "Finish".to_string());
        translations.insert("button.browse".to_string(), "Browse".to_string());
    }
    
    translations
}

/// Set the current locale.
///
/// # Arguments
/// * `locale` - Locale to set (e.g., "en-US", "zh-CN")
///
/// # Returns
/// * Success or error message
#[tauri::command]
pub async fn set_locale(locale: String) -> Result<(), String> {
    info!("Setting locale to: {}", locale);
    
    if let Ok(mut state) = INSTALL_STATE.lock() {
        state.current_locale = locale;
    }
    
    Ok(())
}

/// Minimize the main window.
///
/// This command provides a reliable way to minimize the window from the frontend,
/// working around potential issues with the Tauri 2.0 JavaScript API.
#[tauri::command]
pub async fn minimize_window(app: AppHandle) -> Result<(), String> {
    debug!("Minimizing window");
    
    if let Some(window) = app.get_webview_window("main") {
        window.minimize().map_err(|e| e.to_string())?;
    } else {
        return Err("Main window not found".to_string());
    }
    
    Ok(())
}

/// Close the main window.
///
/// This command provides a reliable way to close the window from the frontend,
/// working around potential issues with the Tauri 2.0 JavaScript API.
#[tauri::command]
pub async fn close_window(app: AppHandle) -> Result<(), String> {
    debug!("Closing window");
    
    if let Some(window) = app.get_webview_window("main") {
        window.close().map_err(|e| e.to_string())?;
    } else {
        return Err("Main window not found".to_string());
    }
    
    Ok(())
}

/// Start dragging the window.
///
/// This command allows the frontend to initiate window dragging,
/// which is necessary for custom titlebars in frameless windows.
#[tauri::command]
pub async fn start_dragging(app: AppHandle) -> Result<(), String> {
    debug!("Starting window drag");
    
    if let Some(window) = app.get_webview_window("main") {
        window.start_dragging().map_err(|e| e.to_string())?;
    } else {
        return Err("Main window not found".to_string());
    }
    
    Ok(())
}
