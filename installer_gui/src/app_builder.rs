use crate::{commands, custom_ui, embedded_package};
use installer_core::Installer;
use installer_shared::WindowConfig;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tauri::Manager;
use tracing::{error, info};

const CUSTOM_UI_INJECT_SCRIPT: &str = include_str!("assets/custom_ui_inject.js");

pub fn run_tauri_app(embedded_package_path: Option<PathBuf>, custom_ui_dir: Option<PathBuf>) {
    info!("Starting Tauri GUI application");

    let _ui_guard = crate::ui_loader::UIResourcesGuard::new();

    if let Some(ref path) = embedded_package_path {
        std::env::set_var(
            "INSTALLER_EMBEDDED_PACKAGE",
            path.to_string_lossy().as_ref(),
        );
        info!("Set INSTALLER_EMBEDDED_PACKAGE={:?}", path);
    }

    if let Some(ref ui_dir) = custom_ui_dir {
        std::env::set_var("INSTALLER_CUSTOM_UI_DIR", ui_dir.to_string_lossy().as_ref());
        info!("Set INSTALLER_CUSTOM_UI_DIR={:?}", ui_dir);
    }

    let window_config = Arc::new(load_window_config(embedded_package_path.as_deref()));

    let mut builder = tauri::Builder::default().invoke_handler(tauri::generate_handler![
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
        embedded_package::get_embedded_package_path,
        custom_ui::get_custom_ui_content,
    ]);

    if let Some(ref ui_dir) = custom_ui_dir {
        let ui_dir_clone = ui_dir.clone();
        info!(
            "Registering customui:// protocol for directory: {:?}",
            ui_dir
        );
        builder = builder.register_asynchronous_uri_scheme_protocol(
            "customui",
            move |_ctx, request, responder| {
                let ui_dir = ui_dir_clone.clone();
                let uri = request.uri();
                let path = uri.path();
                info!("Custom UI request: uri={}, path={}", uri, path);

                let file_path = path.strip_prefix('/').unwrap_or(path);
                let file_path = if file_path.is_empty() || file_path == "/" {
                    "index.html"
                } else {
                    file_path
                };

                let full_path = ui_dir.join(file_path);
                info!("Serving file: {:?}", full_path);

                match std::fs::read(&full_path) {
                    Ok(content) => {
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
            },
        );
    }

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

            if custom_ui_dir.is_some() {
                let window = app.get_webview_window("main");
                if let Some(win) = window {
                    std::thread::spawn(move || {
                        std::thread::sleep(std::time::Duration::from_millis(300));
                        info!("Injecting custom UI content");
                        if let Err(e) = win.eval(CUSTOM_UI_INJECT_SCRIPT) {
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
            let min_w = config.min_width.unwrap_or(logical.width.round() as u32);
            let min_h = config.min_height.unwrap_or(logical.height.round() as u32);
            let _ = window.set_min_size(Some(tauri::Size::Logical(tauri::LogicalSize {
                width: min_w as f64,
                height: min_h as f64,
            })));
        }
    }

    if let Some(resizable) = config.resizable {
        let _ = window.set_resizable(resizable);
    }
}
