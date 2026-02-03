//! Installer CLI - Command-line tool for installing packages.
//!
//! Provides a command-line interface for:
//! - Installing packages with various options
//! - Silent/unattended installation
//! - Uninstalling applications
//! - Progress reporting to console
//!
//! # Requirements
//! - 12.1: Accept --silent flag for non-interactive installation
//! - 12.2: Accept --install-dir parameter for installation directory
//! - 12.3: Accept --no-shortcuts flag to skip shortcut creation
//! - 12.4: Accept --no-registry flag to skip registry operations
//! - 12.5: Accept --uninstall flag to trigger uninstallation
//! - 12.6: Accept --help flag to display usage information
//! - 12.7: Silent mode should not prompt for user input
//! - 12.8: Silent mode should exit with non-zero code on failure
//! - 12.9: Print progress messages to stdout
//! - 12.10: Print error messages to stderr

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use installer_core::{init_cli_logging, Installer, ScriptPolicy, Uninstaller};
use installer_shared::{FlowDefinition, InstallOptions, Phase, ProgressEvent};
use std::io::{self, Write};
use std::path::PathBuf;
use std::process::ExitCode;
use tracing::{debug, error, info, warn};

/// Command-line arguments for the installer.
///
/// # Requirements
/// - 12.1-12.6: Command-line argument support
#[derive(Parser, Debug)]
#[command(name = "installer")]
#[command(author, version, about = "Install and uninstall packages")]
#[command(
    long_about = "A command-line installer for extracting and installing packages.\n\n\
    Use --silent for unattended installations.\n\
    Use --uninstall to remove an installed application."
)]
struct Args {
    /// Subcommand to execute (install or uninstall)
    #[command(subcommand)]
    command: Option<Commands>,

    /// Package file to install (for direct installation without subcommand)
    #[arg(short, long)]
    package: Option<PathBuf>,

    /// Installation directory
    ///
    /// Specifies where to install the application.
    /// If not provided, uses the default from package metadata.
    #[arg(short, long)]
    install_dir: Option<PathBuf>,

    /// Silent installation (no prompts)
    ///
    /// Runs the installer without any user interaction.
    /// Uses default values or command-line specified options.
    /// Exits with non-zero code on failure.
    #[arg(short, long)]
    silent: bool,

    /// Skip creating desktop shortcuts
    ///
    /// Prevents the installer from creating desktop shortcuts
    /// even if the package metadata requests them.
    #[arg(long)]
    no_shortcuts: bool,

    /// Skip registry operations
    ///
    /// Prevents the installer from writing to the Windows registry.
    /// This includes uninstaller registration and custom registry entries.
    #[arg(long)]
    no_registry: bool,

    /// Uninstall mode (legacy flag, prefer 'uninstall' subcommand)
    ///
    /// Triggers uninstallation instead of installation.
    /// Requires --install-dir to specify the installation directory.
    #[arg(long)]
    uninstall: bool,

    /// Number of threads for parallel decompression
    ///
    /// Defaults to the number of CPU cores.
    #[arg(long)]
    threads: Option<usize>,

    /// Optional YAML flow definition path
    ///
    /// If provided, installer will execute this custom flow instead of the default built-in flow.
    #[arg(long)]
    flow: Option<PathBuf>,

    /// Enable script step execution in flow.
    #[arg(long)]
    enable_scripts: bool,

    /// Allowlisted script root (repeatable).
    #[arg(long = "script-allow-root")]
    script_allow_roots: Vec<PathBuf>,

    /// Verbose output
    ///
    /// Enables detailed logging output.
    #[arg(short, long)]
    verbose: bool,

    /// Force installation even if application is running
    ///
    /// Skips the running process check.
    #[arg(long)]
    force: bool,

    /// Skip Windows version check
    ///
    /// Bypasses the minimum Windows version requirement.
    #[arg(long)]
    skip_version_check: bool,
}

/// Subcommands for the installer CLI.
#[derive(Subcommand, Debug)]
enum Commands {
    /// Install a package
    Install {
        /// Package file to install
        #[arg(required = true)]
        package: PathBuf,

        /// Installation directory
        #[arg(short, long)]
        install_dir: Option<PathBuf>,

        /// Silent installation (no prompts)
        #[arg(short, long)]
        silent: bool,

        /// Skip creating desktop shortcuts
        #[arg(long)]
        no_shortcuts: bool,

        /// Skip registry operations
        #[arg(long)]
        no_registry: bool,

        /// Number of threads for parallel decompression
        #[arg(long)]
        threads: Option<usize>,

        /// Optional YAML flow definition path
        #[arg(long)]
        flow: Option<PathBuf>,

        /// Enable script step execution in flow.
        #[arg(long)]
        enable_scripts: bool,

        /// Allowlisted script root (repeatable).
        #[arg(long = "script-allow-root")]
        script_allow_roots: Vec<PathBuf>,

        /// Force installation even if application is running
        #[arg(long)]
        force: bool,

        /// Skip Windows version check
        #[arg(long)]
        skip_version_check: bool,
    },

    /// Uninstall an application
    Uninstall {
        /// Installation directory containing install.manifest.json
        #[arg(required = true)]
        install_dir: PathBuf,

        /// Silent uninstallation (no prompts)
        #[arg(short, long)]
        silent: bool,
    },
}

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("Error: {:#}", e);
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<()> {
    let args = Args::parse();

    // Initialize logging using the new logging system
    init_cli_logging(args.verbose).context("Failed to initialize logging")?;

    // Handle subcommands
    if let Some(command) = args.command {
        return match command {
            Commands::Install {
                package,
                install_dir,
                silent,
                no_shortcuts,
                no_registry,
                threads,
                flow,
                enable_scripts,
                script_allow_roots,
                force,
                skip_version_check,
            } => run_install(InstallArgs {
                package,
                install_dir,
                silent,
                no_shortcuts,
                no_registry,
                threads,
                flow,
                enable_scripts,
                script_allow_roots,
                force,
                skip_version_check,
                verbose: args.verbose,
            }),
            Commands::Uninstall {
                install_dir,
                silent,
            } => run_uninstall(&install_dir, silent, args.verbose),
        };
    }

    // Handle legacy mode (without subcommand)
    if args.uninstall {
        // Uninstall mode
        let install_dir = args
            .install_dir
            .ok_or_else(|| anyhow::anyhow!("--install-dir is required for uninstall mode"))?;
        return run_uninstall(&install_dir, args.silent, args.verbose);
    }

    // Install mode
    let package = args.package.ok_or_else(|| {
        anyhow::anyhow!("Package file is required. Use --package <file> or 'install <file>'")
    })?;

    run_install(InstallArgs {
        package,
        install_dir: args.install_dir,
        silent: args.silent,
        no_shortcuts: args.no_shortcuts,
        no_registry: args.no_registry,
        threads: args.threads,
        flow: args.flow,
        enable_scripts: args.enable_scripts,
        script_allow_roots: args.script_allow_roots,
        force: args.force,
        skip_version_check: args.skip_version_check,
        verbose: args.verbose,
    })
}

/// Arguments for installation.
#[allow(dead_code)]
struct InstallArgs {
    package: PathBuf,
    install_dir: Option<PathBuf>,
    silent: bool,
    no_shortcuts: bool,
    no_registry: bool,
    threads: Option<usize>,
    flow: Option<PathBuf>,
    enable_scripts: bool,
    script_allow_roots: Vec<PathBuf>,
    force: bool,
    skip_version_check: bool,
    verbose: bool,
}

/// Run the installation process.
///
/// # Requirements
/// - 3.10: Report success or failure with error details
/// - 7.8: Print progress to console
/// - 12.7: Silent mode should not prompt for user input
/// - 12.8: Silent mode should exit with non-zero code on failure
fn run_install(args: InstallArgs) -> Result<()> {
    if !args.silent {
        println!("Installer CLI v{}", env!("CARGO_PKG_VERSION"));
        println!();
    }

    // Create installer
    let mut installer = Installer::new(args.package.clone()).context("Failed to open package")?;
    if args.enable_scripts {
        if args.script_allow_roots.is_empty() {
            return Err(anyhow::anyhow!(
                "--enable-scripts requires at least one --script-allow-root"
            ));
        }
        installer = installer.with_script_policy(ScriptPolicy::enabled_with_roots(
            args.script_allow_roots.clone(),
        ));
    }

    // Parse package to get metadata
    let parsed = installer
        .parse_package()
        .context("Failed to parse package")?;

    if !args.silent {
        println!("Application: {}", parsed.metadata.app_name);
        println!("Version: {}", parsed.metadata.version);
        if let Some(ref vendor) = parsed.metadata.vendor {
            println!("Publisher: {}", vendor);
        }
        println!();
    }

    // Determine install directory
    let install_dir = args.install_dir.unwrap_or_else(|| {
        let default = parsed.metadata.default_install_dir.clone();
        // Expand environment variables
        expand_env_vars(&default).join(&parsed.metadata.app_name)
    });

    if !args.silent {
        println!("Installing to: {}", install_dir.display());
        println!();
    }

    // Check prerequisites
    // 1. Check disk space
    info!("Checking disk space...");
    if let Err(e) = installer.check_disk_space(&install_dir) {
        error!("Disk space check failed: {}", e);
        if args.silent {
            return Err(e.into());
        } else {
            eprintln!("Error: Insufficient disk space");
            eprintln!("{}", e);
            return Err(e.into());
        }
    }

    // 2. Check Windows version (unless skipped)
    if !args.skip_version_check {
        info!("Checking Windows version...");
        if let Err(e) = installer.check_windows_version() {
            error!("Windows version check failed: {}", e);
            if args.silent {
                return Err(e.into());
            } else {
                eprintln!("Error: Windows version requirement not met");
                eprintln!("{}", e);
                return Err(e.into());
            }
        }
    } else {
        debug!("Skipping Windows version check");
    }

    // 3. Check for running processes (unless forced)
    if !args.force {
        info!("Checking for running processes...");
        match installer.check_running_process() {
            Ok(true) => {
                let process_name = parsed
                    .metadata
                    .process_name
                    .as_deref()
                    .unwrap_or("the application");
                if args.silent {
                    return Err(anyhow::anyhow!(
                        "Application '{}' is running. Please close it before installing.",
                        process_name
                    ));
                } else {
                    eprintln!("Warning: '{}' appears to be running.", process_name);
                    if !prompt_continue("Do you want to continue anyway?")? {
                        return Err(anyhow::anyhow!("Installation cancelled by user"));
                    }
                }
            }
            Ok(false) => {
                debug!("No conflicting processes detected");
            }
            Err(e) => {
                warn!("Failed to check running processes: {}", e);
                // Continue anyway - this is not a critical error
            }
        }
    } else {
        debug!("Skipping running process check (--force)");
    }

    // Create install options
    let options = InstallOptions {
        install_dir: install_dir.clone(),
        create_shortcuts: !args.no_shortcuts,
        configure_registry: !args.no_registry,
        auto_startup: parsed.metadata.auto_startup,
        components: std::collections::BTreeMap::new(),
        silent: args.silent,
        thread_count: args.threads,
    };

    // Run installation with progress reporting
    let progress_printer = ProgressPrinter::new(args.silent);
    let stats = if let Some(flow_path) = args.flow.as_ref() {
        info!("Using custom flow definition: {}", flow_path.display());
        let flow = FlowDefinition::from_yaml_file(flow_path)
            .with_context(|| format!("Failed to load flow file: {}", flow_path.display()))?;
        installer.install_with_flow_definition(options, flow, |event| {
            progress_printer.print(&event);
        })?
    } else {
        installer.install(options, |event| {
            progress_printer.print(&event);
        })?
    };

    // Ensure we print a newline after progress bar
    if !args.silent {
        println!();
    }

    // Create uninstaller
    info!("Creating uninstaller...");
    if let Err(e) = installer.create_uninstaller(&install_dir) {
        warn!("Failed to create uninstaller: {}", e);
        if !args.silent {
            eprintln!("Warning: Failed to create uninstaller: {}", e);
        }
    }

    // Print completion message
    if !args.silent {
        println!();
        println!("Installation complete!");
        println!("  Files installed: {}", stats.installed_files);
        println!("  Total size: {}", format_size(stats.total_size));
        println!("  Time: {:.2}s", stats.elapsed_time.as_secs_f64());
    }

    info!(
        "Installation completed: {} files, {} bytes",
        stats.installed_files, stats.total_size
    );

    Ok(())
}

/// Run the uninstallation process.
///
/// # Requirements
/// - 12.5: Accept --uninstall flag to trigger uninstallation
fn run_uninstall(install_dir: &PathBuf, silent: bool, _verbose: bool) -> Result<()> {
    if !silent {
        println!(
            "Installer CLI v{} - Uninstall Mode",
            env!("CARGO_PKG_VERSION")
        );
        println!();
    }

    // Create uninstaller from install directory
    let uninstaller = Uninstaller::from_install_dir(install_dir)
        .context("Failed to read installation manifest")?;

    let manifest = uninstaller.manifest();

    if !silent {
        println!("Application: {}", manifest.app_name);
        println!("Version: {}", manifest.version);
        println!("Install directory: {}", manifest.install_dir);
        println!();

        if !prompt_continue("Are you sure you want to uninstall?")? {
            println!("Uninstallation cancelled.");
            return Ok(());
        }
        println!();
    }

    // Run uninstallation with progress reporting
    let progress_printer = ProgressPrinter::new(silent);
    let stats = uninstaller.uninstall(|event| {
        progress_printer.print(&event);
    })?;

    // Ensure we print a newline after progress
    if !silent {
        println!();
    }

    // Print completion message
    if !silent {
        println!();
        println!("Uninstallation complete!");
        println!("  Files deleted: {}", stats.files_deleted);
        println!("  Directories removed: {}", stats.directories_removed);
        println!(
            "  Registry entries cleaned: {}",
            stats.registry_entries_cleaned
        );

        if !stats.errors.is_empty() {
            println!();
            println!("Some items could not be removed:");
            for error in &stats.errors {
                println!("  - {}", error);
            }
        }
    }

    info!(
        "Uninstallation completed: {} files deleted, {} directories removed",
        stats.files_deleted, stats.directories_removed
    );

    Ok(())
}

/// Expand environment variables in a path string.
fn expand_env_vars(path: &str) -> PathBuf {
    let expanded = if path.contains('%') {
        // Windows-style environment variables
        let mut result = path.to_string();
        for (key, value) in std::env::vars() {
            let pattern = format!("%{}%", key);
            result = result.replace(&pattern, &value);
        }
        result
    } else {
        path.to_string()
    };
    PathBuf::from(expanded)
}

/// Prompt the user for confirmation.
///
/// Returns `true` if the user confirms, `false` otherwise.
/// In silent mode, this should never be called.
fn prompt_continue(message: &str) -> Result<bool> {
    print!("{} [y/N]: ", message);
    io::stdout().flush()?;

    let mut input = String::new();
    io::stdin().read_line(&mut input)?;

    let response = input.trim().to_lowercase();
    Ok(response == "y" || response == "yes")
}

/// Format a size in bytes to a human-readable string.
fn format_size(bytes: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;

    if bytes >= GB {
        format!("{:.2} GB", bytes as f64 / GB as f64)
    } else if bytes >= MB {
        format!("{:.2} MB", bytes as f64 / MB as f64)
    } else if bytes >= KB {
        format!("{:.2} KB", bytes as f64 / KB as f64)
    } else {
        format!("{} bytes", bytes)
    }
}

/// Progress printer for console output.
///
/// Handles printing progress events to the console with a progress bar.
/// In silent mode, no output is printed.
///
/// # Requirements
/// - 7.8: Print progress to console
/// - 12.9: Print progress messages to stdout
struct ProgressPrinter {
    silent: bool,
    last_phase: std::sync::Mutex<Option<Phase>>,
}

impl ProgressPrinter {
    fn new(silent: bool) -> Self {
        Self {
            silent,
            last_phase: std::sync::Mutex::new(None),
        }
    }

    fn print(&self, event: &ProgressEvent) {
        if self.silent {
            return;
        }

        // Check if phase changed
        let mut last_phase = self.last_phase.lock().unwrap();
        let phase_changed = *last_phase != Some(event.phase);
        if phase_changed {
            // Print newline before new phase (except for first phase)
            if last_phase.is_some() {
                println!();
            }
            *last_phase = Some(event.phase);
        }
        drop(last_phase); // Release lock before printing

        let phase_str = match event.phase {
            Phase::Scanning => "Scanning",
            Phase::Compressing => "Compressing",
            Phase::Decompressing => "Extracting",
            Phase::Writing => "Writing",
            Phase::Completing => "Finishing",
        };

        let percentage = event.percentage();
        let bar_width: usize = 40;
        let filled = (percentage / 100.0 * bar_width as f64) as usize;
        let empty = bar_width.saturating_sub(filled);

        // Build progress line
        let mut line = format!(
            "\r{:12} [{}{}] {:5.1}%",
            phase_str,
            "=".repeat(filled),
            " ".repeat(empty),
            percentage
        );

        // Add current file if available
        if let Some(ref file) = event.current_file {
            line.push_str(&format!(" {}", truncate_path(file, 30)));
        }

        // Add speed if available
        if let Some(speed) = event.speed_bps {
            line.push_str(&format!(" ({})", format_speed(speed)));
        }

        // Add message if available and no file
        if event.current_file.is_none() {
            if let Some(ref message) = event.message {
                line.push_str(&format!(" {}", truncate_path(message, 40)));
            }
        }

        // Clear rest of line and print
        print!("{}\x1b[K", line);
        io::stdout().flush().ok();
    }
}

/// Truncate a path string for display.
fn truncate_path(path: &str, max_len: usize) -> String {
    if path.len() <= max_len {
        path.to_string()
    } else {
        format!("...{}", &path[path.len() - max_len + 3..])
    }
}

/// Format speed in bytes per second to a human-readable string.
fn format_speed(bps: u64) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;

    if bps >= MB {
        format!("{:.1} MB/s", bps as f64 / MB as f64)
    } else if bps >= KB {
        format!("{:.1} KB/s", bps as f64 / KB as f64)
    } else {
        format!("{} B/s", bps)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_expand_env_vars() {
        // Test with no variables
        let path = expand_env_vars("/usr/local/bin");
        assert_eq!(path, PathBuf::from("/usr/local/bin"));

        // Test with Windows-style variable (if set)
        std::env::set_var("TEST_VAR", "test_value");
        let path = expand_env_vars("%TEST_VAR%/subdir");
        assert_eq!(path, PathBuf::from("test_value/subdir"));
        std::env::remove_var("TEST_VAR");
    }

    #[test]
    fn test_format_size() {
        assert_eq!(format_size(0), "0 bytes");
        assert_eq!(format_size(512), "512 bytes");
        assert_eq!(format_size(1024), "1.00 KB");
        assert_eq!(format_size(1536), "1.50 KB");
        assert_eq!(format_size(1048576), "1.00 MB");
        assert_eq!(format_size(1073741824), "1.00 GB");
    }

    #[test]
    fn test_format_speed() {
        assert_eq!(format_speed(512), "512 B/s");
        assert_eq!(format_speed(1024), "1.0 KB/s");
        assert_eq!(format_speed(1048576), "1.0 MB/s");
    }

    #[test]
    fn test_truncate_path() {
        assert_eq!(truncate_path("short", 10), "short");
        assert_eq!(
            truncate_path("this/is/a/very/long/path", 15),
            "...ry/long/path"
        );
    }

    #[test]
    fn test_progress_printer_silent() {
        let printer = ProgressPrinter::new(true);
        // Should not panic or print anything
        printer.print(&ProgressEvent::new(Phase::Scanning, 0, 100));
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use installer_core::Packager;
    use installer_shared::PackagerConfig;
    use proptest::prelude::*;
    use std::fs;
    use std::sync::Arc;
    use tempfile::tempdir;

    /// Generate random file content of varying sizes
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 100..2000)
    }

    /// Generate a valid filename
    fn filename_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_]{1,8}\\.(txt|bin|dat)"
    }

    /// Generate a list of files with content
    fn files_strategy() -> impl Strategy<Value = Vec<(String, Vec<u8>)>> {
        prop::collection::vec((filename_strategy(), file_content_strategy()), 1..3)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(10))]

        /// **Property 18: Silent Mode Non-Interactivity**
        ///
        /// For any installation operation, if the --silent flag is used,
        /// there should be no interactive prompts requiring user input.
        ///
        /// **Validates: Requirements 12.7**
        ///
        /// This test verifies that:
        /// 1. ProgressPrinter in silent mode produces no output
        /// 2. Silent mode flag is properly propagated through the system
        /// 3. No prompts are shown during silent installation
        #[test]
        fn prop_silent_mode_no_output(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let _install_dir = tempdir().expect("Failed to create install dir");

            // Create unique filenames to avoid collisions
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Create a silent progress printer and verify it doesn't output
            let printer = ProgressPrinter::new(true); // silent = true

            // In silent mode, the printer should not produce any output
            // We verify this by checking that the print method returns immediately
            // without modifying any state when silent is true
            prop_assert!(printer.silent, "Printer should be in silent mode");

            // Verify that printing in silent mode doesn't panic and doesn't
            // modify the last_phase state (since it returns early)
            let event = ProgressEvent::new(Phase::Scanning, 0, 100);
            printer.print(&event);

            // The last_phase should still be None because silent mode returns early
            let last_phase = printer.last_phase.lock().unwrap();
            prop_assert!(last_phase.is_none(),
                "Silent mode should not update internal state");
        }

        /// **Property 18 (continued): Silent Installation Completes Without Prompts**
        ///
        /// Verifies that a complete installation can run in silent mode
        /// without requiring any user interaction.
        ///
        /// **Validates: Requirements 12.7, 12.8**
        #[test]
        fn prop_silent_installation_no_prompts(
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

            // Create installer
            let installer = installer_core::Installer::new(output_path)
                .expect("Failed to create installer");

            // Create install options with silent mode
            let options = installer_shared::InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,  // Silent mode enabled
                thread_count: None,
            };

            // Track progress events
            let events_received = Arc::new(std::sync::Mutex::new(Vec::new()));
            let events_clone = events_received.clone();

            // Run installation - should complete without any prompts
            let result = installer.install(options, move |event| {
                events_clone.lock().unwrap().push(event.clone());
            });

            // Installation should succeed
            prop_assert!(result.is_ok(), "Silent installation should succeed");

            // Verify files were installed
            for name in &created_files {
                let installed_path = install_dir.path().join(name);
                prop_assert!(installed_path.exists(),
                    "File {} should be installed", name);
            }

            // Verify progress events were received (even in silent mode,
            // the core still emits events - it's the CLI that doesn't print them)
            let events = events_received.lock().unwrap();
            prop_assert!(!events.is_empty(),
                "Progress events should still be emitted in silent mode");
        }
    }
}
