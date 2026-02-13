use crate::app_builder::run_tauri_app;
use crate::custom_ui::extract_custom_ui_resources;
use crate::embedded_package::check_and_extract_embedded_package;
use crate::webview2::{check_webview2, WebView2Action};
use installer_core::init_gui_logging;
use std::path::{Path, PathBuf};
use tracing::{error, info, warn};

pub fn run() {
    match init_gui_logging() {
        Ok(log_path) => info!("Logging to: {:?}", log_path),
        Err(e) => eprintln!("Warning: Failed to initialize logging: {}", e),
    }

    let embedded_package = check_and_extract_embedded_package();
    let embedded_package_path = embedded_package.as_ref().map(|p| p.package_path.clone());

    let custom_ui = if let Some(ref package_path) = embedded_package_path {
        extract_custom_ui_resources(package_path)
    } else {
        None
    };
    let custom_ui_dir = custom_ui.as_ref().map(|ui| ui.dir.clone());

    #[cfg(windows)]
    {
        let webview2_status = check_webview2();
        if !webview2_status.is_installed {
            warn!("WebView2 runtime not found");
            match show_webview2_prompt() {
                WebView2Action::Install => {
                    info!("User chose to install WebView2");
                    if let Err(e) = crate::webview2::open_download_page() {
                        error!("Failed to open download page: {}", e);
                    }
                    std::process::exit(0);
                }
                WebView2Action::UseCli => {
                    info!("User chose CLI mode");
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

    // Keep `embedded_package` and `custom_ui` in scope to preserve temp dirs.
    let _embedded_guard = embedded_package;
    let _custom_ui_guard = custom_ui;
    run_tauri_app(embedded_package_path, custom_ui_dir);
}

#[cfg(windows)]
fn show_webview2_prompt() -> WebView2Action {
    use std::ffi::CString;

    let title = CString::new("WebView2 Runtime Required").unwrap();
    let message = CString::new(
        "The WebView2 runtime is required to run this installer's graphical interface.\n\n\
         Would you like to:\n\
         • Click 'Yes' to download and install WebView2\n\
         • Click 'No' to continue with command-line mode\n\
         • Click 'Cancel' to exit",
    )
    .unwrap();

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

fn run_cli_mode(embedded_package_path: Option<&Path>) {
    info!("Running in CLI mode (WebView2 not available)");

    let args: Vec<String> = std::env::args().collect();

    if args.len() <= 1 && embedded_package_path.is_none() {
        println!("Installer - Command Line Mode");
        println!();
        println!("WebView2 runtime is not installed. Running in CLI mode.");
        println!();
        println!(
            "Usage: {} [OPTIONS]",
            args.first().map(|s| s.as_str()).unwrap_or("installer")
        );
        println!();
        println!("Options:");
        println!("  --install-dir <PATH>  Installation directory");
        println!("  --silent              Silent installation (no prompts)");
        println!("  --no-shortcuts        Don't create desktop shortcuts");
        println!("  --help                Show this help message");
        println!();
        println!("To use the graphical installer, please install WebView2 from:");
        println!("  {}", crate::webview2::WEBVIEW2_DOWNLOAD_URL);
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
            _ => eprintln!("Unknown option: {}", args[i]),
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
                    install_dir: PathBuf::from(&target_dir),
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
