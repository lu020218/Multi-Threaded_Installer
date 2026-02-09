//! Installer GUI - Tauri-based graphical installer.
//!
//! This module provides a modern web-based GUI for the installer using Tauri.
//! The installer can run as a self-contained executable with embedded package data,
//! or load a package from an external file.
//!
//! # Architecture
//!
//! The installer supports two UI loading modes:
//! 1. **Embedded UI (default)**: Uses the UI resources compiled into the Tauri executable
//! 2. **Custom UI (from package)**: Extracts UI resources from the package to a temp directory
//!    and loads them at runtime
//!
//! When a package contains custom UI resources, they are extracted to a temporary directory
//! and served via a custom protocol, allowing full customization without recompiling.
//!
//! # Requirements
//! - 4.1: Use Tauri framework for cross-platform GUI
//! - 4.2: Check WebView2 runtime on Windows startup
//! - 4.3: Display installation prompt if WebView2 not found
//! - 4.4: Start main installation window when WebView2 available

#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

mod commands;
mod events;
mod ui_loader;
mod webview2;

use installer_core::{check_embedded_package, extract_embedded_package, init_gui_logging, Installer};
use installer_shared::WindowConfig;
use std::path::{Path, PathBuf};
use tempfile::TempDir;
use tauri::Manager;
use tracing::{error, info, warn};
use webview2::{check_webview2, WebView2Action};

// Global state for embedded package
static mut EMBEDDED_PACKAGE_PATH: Option<PathBuf> = None;
static mut TEMP_DIR: Option<TempDir> = None;

fn main() {
    // Initialize GUI-mode logging (writes to temp directory file)
    match init_gui_logging() {
        Ok(log_path) => {
            info!("Logging to: {:?}", log_path);
        }
        Err(e) => {
            eprintln!("Warning: Failed to initialize logging: {}", e);
        }
    }

    // Check for embedded package data in the current executable
    let embedded_package_path = check_and_extract_embedded_package();

    // Try to extract custom UI resources from the package
    let custom_ui_dir = if let Some(ref package_path) = embedded_package_path {
        extract_custom_ui_resources(package_path)
    } else {
        None
    };

    // Check WebView2 availability on Windows
    #[cfg(windows)]
    {
        let webview2_status = check_webview2();

        if !webview2_status.is_installed {
            warn!("WebView2 runtime not found");

            // Show a message box asking the user what to do
            match show_webview2_prompt() {
                WebView2Action::Install => {
                    info!("User chose to install WebView2");
                    if let Err(e) = webview2::open_download_page() {
                        error!("Failed to open download page: {}", e);
                    }
                    // Exit and let user restart after installing WebView2
                    std::process::exit(0);
                }
                WebView2Action::UseCli => {
                    info!("User chose CLI mode");
                    // Fall through to CLI mode
                    run_cli_mode(embedded_package_path.as_deref());
                    return;
                }
                WebView2Action::Cancel => {
                    info!("User cancelled installation");
                    std::process::exit(0);
                }
            }
        } else {
            info!(
                "WebView2 runtime found: version {}",
                webview2_status.version.unwrap_or_default()
            );
        }
    }

    // Run the Tauri application
    run_tauri_app(embedded_package_path, custom_ui_dir);
}

/// Extract custom UI resources from the embedded package.
fn extract_custom_ui_resources(package_path: &Path) -> Option<PathBuf> {
    info!("Checking for custom UI resources in package: {:?}", package_path);

    let installer = match Installer::new(package_path.to_path_buf()) {
        Ok(i) => i,
        Err(e) => {
            warn!("Failed to open package for UI extraction: {}", e);
            return None;
        }
    };

    // Check if package has UI resources
    let has_ui = match installer.has_ui_resources() {
        Ok(has) => has,
        Err(e) => {
            warn!("Failed to check UI resources: {}", e);
            return None;
        }
    };

    if !has_ui {
        info!("Package does not contain custom UI resources, using default UI");
        return None;
    }

    // Create temporary directory for UI resources
    let temp_dir = match tempfile::tempdir() {
        Ok(dir) => dir,
        Err(e) => {
            error!("Failed to create temp directory for UI: {}", e);
            return None;
        }
    };

    let ui_dir = temp_dir.path().to_path_buf();

    // Extract UI resources
    match installer.extract_ui_resources(&ui_dir) {
        Ok(Some(_)) => {
            info!("Extracted custom UI resources to: {:?}", ui_dir);
            
            // Keep the temp directory alive by leaking it
            // It will be cleaned up when the process exits
            std::mem::forget(temp_dir);
            
            Some(ui_dir)
        }
        Ok(None) => {
            info!("No UI resources to extract");
            None
        }
        Err(e) => {
            error!("Failed to extract UI resources: {}", e);
            None
        }
    }
}

/// Check for embedded package data and extract it if present.
fn check_and_extract_embedded_package() -> Option<PathBuf> {
    let exe_path = match std::env::current_exe() {
        Ok(path) => path,
        Err(e) => {
            warn!("Failed to get current executable path: {}", e);
            return None;
        }
    };

    // Check if this executable has embedded package data
    match check_embedded_package(&exe_path) {
        Ok(Some(marker)) => {
            info!(
                "Found embedded package data at offset {}",
                marker.package_offset
            );

            // Create a temporary directory for the extracted package
            let temp_dir = match tempfile::tempdir() {
                Ok(dir) => dir,
                Err(e) => {
                    error!("Failed to create temp directory: {}", e);
                    return None;
                }
            };

            let package_path = temp_dir.path().join("embedded.pkg");

            // Extract the embedded package
            match extract_embedded_package(&exe_path, &package_path) {
                Ok(size) => {
                    info!("Extracted embedded package: {} bytes", size);

                    // Store the temp dir to prevent cleanup
                    unsafe {
                        EMBEDDED_PACKAGE_PATH = Some(package_path.clone());
                        TEMP_DIR = Some(temp_dir);
                    }

                    Some(package_path)
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


/// Run the Tauri GUI application.
fn run_tauri_app(embedded_package_path: Option<PathBuf>, custom_ui_dir: Option<PathBuf>) {
    info!("Starting Tauri GUI application");

    // Try to extract embedded UI resources
    let _ui_guard = ui_loader::UIResourcesGuard::new();

    // Store the embedded package path for commands to use
    if let Some(ref path) = embedded_package_path {
        std::env::set_var("INSTALLER_EMBEDDED_PACKAGE", path.to_string_lossy().as_ref());
        info!("Set INSTALLER_EMBEDDED_PACKAGE={:?}", path);
    }

    // Store custom UI directory path if available
    if let Some(ref ui_dir) = custom_ui_dir {
        std::env::set_var("INSTALLER_CUSTOM_UI_DIR", ui_dir.to_string_lossy().as_ref());
        info!("Set INSTALLER_CUSTOM_UI_DIR={:?}", ui_dir);
    }

    let window_config = load_window_config(embedded_package_path.as_deref());
    let window_config = std::sync::Arc::new(window_config);

    // Build Tauri app with optional custom UI
    let mut builder = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            commands::get_metadata,
            commands::validate_install_request,
            commands::start_install,
            commands::cancel_install,
            commands::get_system_locale,
            commands::check_prerequisites,
            commands::check_webview2_status,
            commands::browse_directory,
            commands::get_translations,
            commands::set_locale,
            commands::minimize_window,
            commands::close_window,
            commands::start_dragging,
            commands::apply_window_config,
            commands::notify_ui_ready,
            get_embedded_package_path,
            get_custom_ui_content,
        ]);

    // If we have custom UI resources, register a custom protocol to serve them
    // This is used for loading CSS, JS, images, and other assets
    if let Some(ref ui_dir) = custom_ui_dir {
        let ui_dir_clone = ui_dir.clone();
        info!("Registering customui:// protocol for directory: {:?}", ui_dir);
        builder = builder.register_asynchronous_uri_scheme_protocol("customui", move |_ctx, request, responder| {
            let ui_dir = ui_dir_clone.clone();
            
            // Parse the request path
            let uri = request.uri();
            let path = uri.path();
            
            info!("Custom UI request: uri={}, path={}", uri, path);
            
            // Remove leading slash and decode
            let file_path = if path.starts_with('/') {
                &path[1..]
            } else {
                path
            };
            
            // Default to index.html
            let file_path = if file_path.is_empty() || file_path == "/" {
                "index.html"
            } else {
                file_path
            };
            
            let full_path = ui_dir.join(file_path);
            info!("Serving file: {:?}", full_path);
            
            // Read and serve the file
            match std::fs::read(&full_path) {
                Ok(content) => {
                    // Determine content type
                    let content_type = match full_path.extension().and_then(|e| e.to_str()) {
                        Some("html") => "text/html",
                        Some("css") => "text/css",
                        Some("js") => "application/javascript",
                        Some("json") => "application/json",
                        Some("png") => "image/png",
                        Some("jpg") | Some("jpeg") => "image/jpeg",
                        Some("svg") => "image/svg+xml",
                        Some("ico") => "image/x-icon",
                        _ => "application/octet-stream",
                    };
                    
                    info!("Serving {} bytes as {}", content.len(), content_type);
                    
                    let response = tauri::http::Response::builder()
                        .status(200)
                        .header("Content-Type", content_type)
                        .header("Access-Control-Allow-Origin", "*")
                        .body(content)
                        .unwrap();
                    responder.respond(response);
                }
                Err(e) => {
                    error!("Failed to read file {:?}: {}", full_path, e);
                    let response = tauri::http::Response::builder()
                        .status(404)
                        .body(format!("Not Found: {:?}", full_path).into_bytes())
                        .unwrap();
                    responder.respond(response);
                }
            }
        });
    }

      // Run the app
      builder
          .setup(move |app| {
              let has_custom_ui = custom_ui_dir.is_some();
              let window_config = window_config.clone();
              if let Some(window) = app.get_webview_window("main") {
                  let _ = window.hide();
                  if let Some(ref cfg) = *window_config {
                      apply_window_config_to_window(&window, cfg);
                  }
                  if !has_custom_ui {
                      let _ = window.show();
                      let _ = window.set_focus();
                  }
              }
              // If we have custom UI, inject it into the page dynamically
              // This keeps the Tauri IPC bridge working while loading custom content
              if custom_ui_dir.is_some() {
                  let window = app.get_webview_window("main");
                  if let Some(win) = window {
                    // Use a small delay to ensure webview is ready
                    std::thread::spawn(move || {
                        std::thread::sleep(std::time::Duration::from_millis(300));
                        info!("Injecting custom UI content");
                        
                        // JavaScript to load and inject custom UI
                        // We preserve the Tauri IPC bridge by not navigating away
                          let script = r#"
                              (async function() {
                                  try {
                                      console.log('Loading custom UI content...');
                                    
                                    // Wait for Tauri API to be available
                                    let attempts = 0;
                                    while (!window.__TAURI__ && attempts < 50) {
                                        await new Promise(r => setTimeout(r, 100));
                                        attempts++;
                                    }
                                    
                                    if (!window.__TAURI__) {
                                        console.error('Tauri API not available');
                                        return;
                                    }
                                    
                                    // Store Tauri API globally for custom scripts (handle Tauri 1.x/2.x)
                                    const resolveInvoke = () => (
                                        (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) ||
                                        (window.__TAURI__ && window.__TAURI__.invoke) ||
                                        (window.__TAURI__ && window.__TAURI__.tauri && window.__TAURI__.tauri.invoke)
                                    );
                                    const resolveListen = () => (
                                        (window.__TAURI__ && window.__TAURI__.event && window.__TAURI__.event.listen)
                                    );
                                    
                                    const tauriInvoke = resolveInvoke();
                                    const tauriListen = resolveListen();
                                    if (!tauriInvoke || !tauriListen) {
                                        console.error('Tauri API incomplete: invoke/listen not available');
                                        return;
                                    }
                                    
                                    window.tauriInvoke = tauriInvoke;
                                    window.tauriListen = tauriListen;
                                    
                                    // Get custom UI content from Rust backend
                                      const content = await tauriInvoke('get_custom_ui_content');
                                    
                                      if (content && content.html) {
                                          console.log('Injecting custom HTML...');
                                        
                                        // Parse the HTML
                                        const parser = new DOMParser();
                                        const doc = parser.parseFromString(content.html, 'text/html');
                                        
                                        // Clear existing styles (except Tauri's)
                                        const existingStyles = document.querySelectorAll('style:not([data-tauri])');
                                        existingStyles.forEach(s => s.remove());
                                        
                                        // Remove existing link stylesheets
                                        const existingLinks = document.querySelectorAll('link[rel="stylesheet"]');
                                        existingLinks.forEach(l => l.remove());
                                        
                                        // Inject custom CSS first
                                        if (content.css) {
                                            const style = document.createElement('style');
                                            style.id = 'custom-ui-styles';
                                            style.textContent = content.css;
                                            document.head.appendChild(style);
                                            console.log('Custom CSS injected');
                                        }
                                        
                                        // Replace body content
                                        document.body.innerHTML = doc.body.innerHTML;
                                        
                                        // Copy body attributes
                                        for (const attr of doc.body.attributes) {
                                            document.body.setAttribute(attr.name, attr.value);
                                        }
                                        
                                        // Inject custom JS after DOM is ready
                                          if (content.js) {
                                              // Wait a bit for DOM to settle
                                              await new Promise(r => setTimeout(r, 100));
                                              
                                              try {
                                                // Execute JS using indirect eval to run in global scope
                                                // (0, eval)(code) runs in global scope, not local scope
                                                (0, eval)(content.js);
                                                console.log('Custom JS executed via indirect eval');
                                                
                                                // Wait for script to execute
                                                await new Promise(r => setTimeout(r, 100));
                                                
                                                // Manually trigger initialization since DOMContentLoaded already fired
                                                if (typeof window.initializeApp === 'function') {
                                                    console.log('Calling window.initializeApp...');
                                                    try {
                                                        await window.initializeApp();
                                                        console.log('initializeApp completed');
                                                    } catch (initErr) {
                                                        console.error('initializeApp error:', initErr);
                                                    }
                                                } else {
                                                    console.warn('window.initializeApp not found after script execution');
                                                }
                                              } catch (e) {
                                                  console.error('Error executing custom JS:', e);
                                              }
                                          }
                                          
                                          console.log('Custom UI injection complete');
                                      } else {
                                          console.log('No custom UI content available, using default UI');
                                      }
                                  } catch (error) {
                                      console.error('Failed to load custom UI:', error);
                                  } finally {
                                      try {
                                          if (typeof tauriInvoke === 'function') {
                                              await tauriInvoke('notify_ui_ready');
                                          } else if (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) {
                                              await window.__TAURI__.core.invoke('notify_ui_ready');
                                          }
                                      } catch (e) {
                                          console.warn('notify_ui_ready failed:', e);
                                      }
                                  }
                              })();
                          "#;
                        
                        if let Err(e) = win.eval(script) {
                            error!("Failed to inject custom UI: {}", e);
                        }
                    });
                }
            }
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

fn load_window_config(package_path: Option<&Path>) -> Option<WindowConfig> {
    let path = package_path?;
    let installer = Installer::new(path.to_path_buf()).ok()?;
    let parsed = installer.parse_package().ok()?;
    parsed.metadata.window
}

fn apply_window_config_to_window(window: &tauri::WebviewWindow, config: &WindowConfig) {
    if let (Some(w), Some(h)) = (config.width, config.height) {
        let _ = window.set_size(tauri::Size::Logical(tauri::LogicalSize {
            width: w as f64,
            height: h as f64,
        }));
    }

    if config.min_width.is_some() || config.min_height.is_some() {
        if let (Ok(current_size), Ok(scale)) = (window.inner_size(), window.scale_factor()) {
            let logical = current_size.to_logical::<f64>(scale);
            let min_w = config
                .min_width
                .unwrap_or(logical.width.round() as u32);
            let min_h = config
                .min_height
                .unwrap_or(logical.height.round() as u32);
            let _ = window.set_min_size(Some(tauri::Size::Logical(
                tauri::LogicalSize {
                    width: min_w as f64,
                    height: min_h as f64,
                },
            )));
        }
    }

    if let Some(resizable) = config.resizable {
        let _ = window.set_resizable(resizable);
    }
}

/// Tauri command to get custom UI content (HTML, CSS, JS).
#[tauri::command]
fn get_custom_ui_content() -> Option<CustomUIContent> {
    let ui_dir = std::env::var("INSTALLER_CUSTOM_UI_DIR").ok()?;
    let ui_path = std::path::Path::new(&ui_dir);
    
    info!("Loading custom UI content from: {:?}", ui_path);
    
    // Read HTML
    let html_path = ui_path.join("index.html");
    let html = std::fs::read_to_string(&html_path).ok()?;
    
    // Read CSS (from styles/main.css)
    let css_path = ui_path.join("styles").join("main.css");
    let css = std::fs::read_to_string(&css_path).ok();
    
    // Read JS (from scripts/main.js)
    let js_path = ui_path.join("scripts").join("main.js");
    let js = std::fs::read_to_string(&js_path).ok();
    
    info!("Loaded custom UI: html={} bytes, css={:?} bytes, js={:?} bytes",
        html.len(),
        css.as_ref().map(|s| s.len()),
        js.as_ref().map(|s| s.len())
    );
    
    Some(CustomUIContent { html, css, js })
}

/// Custom UI content structure
#[derive(serde::Serialize)]
struct CustomUIContent {
    html: String,
    css: Option<String>,
    js: Option<String>,
}

/// Tauri command to get the embedded package path.
#[tauri::command]
fn get_embedded_package_path() -> Option<String> {
    std::env::var("INSTALLER_EMBEDDED_PACKAGE").ok()
}

/// Show a prompt to the user when WebView2 is not installed.
#[cfg(windows)]
fn show_webview2_prompt() -> WebView2Action {
    use std::ffi::CString;
    
    let title = CString::new("WebView2 Runtime Required").unwrap();
    let message = CString::new(
        "The WebView2 runtime is required to run this installer's graphical interface.\n\n\
         Would you like to:\n\
         • Click 'Yes' to download and install WebView2\n\
         • Click 'No' to continue with command-line mode\n\
         • Click 'Cancel' to exit"
    ).unwrap();
    
    let result = unsafe {
        windows_sys::Win32::UI::WindowsAndMessaging::MessageBoxA(
            0,
            message.as_ptr() as *const u8,
            title.as_ptr() as *const u8,
            windows_sys::Win32::UI::WindowsAndMessaging::MB_YESNOCANCEL 
                | windows_sys::Win32::UI::WindowsAndMessaging::MB_ICONQUESTION,
        )
    };
    
    match result {
        windows_sys::Win32::UI::WindowsAndMessaging::IDYES => WebView2Action::Install,
        windows_sys::Win32::UI::WindowsAndMessaging::IDNO => WebView2Action::UseCli,
        _ => WebView2Action::Cancel,
    }
}


/// Run in CLI mode as a fallback when WebView2 is not available.
fn run_cli_mode(embedded_package_path: Option<&std::path::Path>) {
    info!("Running in CLI mode (WebView2 not available)");

    let args: Vec<String> = std::env::args().collect();

    if args.len() <= 1 && embedded_package_path.is_none() {
        println!("Installer - Command Line Mode");
        println!();
        println!("WebView2 runtime is not installed. Running in CLI mode.");
        println!();
        println!(
            "Usage: {} [OPTIONS]",
            args.get(0).map(|s| s.as_str()).unwrap_or("installer")
        );
        println!();
        println!("Options:");
        println!("  --install-dir <PATH>  Installation directory");
        println!("  --silent              Silent installation (no prompts)");
        println!("  --no-shortcuts        Don't create desktop shortcuts");
        println!("  --help                Show this help message");
        println!();
        println!("To use the graphical installer, please install WebView2 from:");
        println!("  {}", webview2::WEBVIEW2_DOWNLOAD_URL);
        return;
    }

    let mut install_dir: Option<String> = None;
    let mut silent = false;
    let mut create_shortcuts = true;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--install-dir" => {
                i += 1;
                if i < args.len() {
                    install_dir = Some(args[i].clone());
                }
            }
            "--silent" => silent = true,
            "--no-shortcuts" => create_shortcuts = false,
            "--help" | "-h" => {
                println!("Use --help for usage information");
                return;
            }
            _ => {
                eprintln!("Unknown option: {}", args[i]);
            }
        }
        i += 1;
    }

    if let Some(package_path) = embedded_package_path {
        println!("Installing from embedded package...");

        match installer_core::Installer::new(package_path.to_path_buf()) {
            Ok(installer) => {
                let metadata = installer.parse_package().ok();
                let default_dir = metadata
                    .as_ref()
                    .map(|m| m.metadata.default_install_dir.clone())
                    .unwrap_or_else(|| "C:\\Program Files\\App".to_string());

                let target_dir = install_dir.unwrap_or(default_dir);

                let options = installer_shared::InstallOptions {
                    install_dir: std::path::PathBuf::from(&target_dir),
                    create_shortcuts,
                    configure_registry: true,
                    auto_startup: false,
                    components: std::collections::BTreeMap::new(),
                    silent,
                    thread_count: None,
                };

                println!("Installing to: {}", target_dir);

                match installer.install(options, |event| {
                    if !silent {
                        print!("\rProgress: {:.1}%", event.percentage());
                        use std::io::Write;
                        std::io::stdout().flush().ok();
                    }
                }) {
                    Ok(stats) => {
                        println!();
                        println!("Installation complete!");
                        println!("  Files installed: {}", stats.installed_files);
                        println!("  Total size: {} bytes", stats.total_size);
                    }
                    Err(e) => {
                        eprintln!();
                        eprintln!("Installation failed: {}", e);
                        std::process::exit(1);
                    }
                }
            }
            Err(e) => {
                eprintln!("Failed to load package: {}", e);
                std::process::exit(1);
            }
        }
    } else {
        println!("CLI mode installation:");
        println!("  Install directory: {:?}", install_dir);
        println!("  Silent: {}", silent);
        println!("  Create shortcuts: {}", create_shortcuts);
        println!();
        println!("Note: No embedded package found. This installer template needs to be");
        println!("packaged with application data using the packager tool.");
    }
}
