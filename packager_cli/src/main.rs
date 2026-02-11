//! Packager CLI - Command-line tool for creating self-contained installer executables.
//!
//! This tool creates a single executable installer that users can run directly.
//! The installer contains both the installation logic and the compressed package data.

use anyhow::{Context, Result};
use clap::Parser;
use installer_core::{init_cli_logging, Packager};
use installer_shared::{PackagerConfig, Phase, ProgressEvent};
use std::path::PathBuf;
use tracing::{info, warn};

/// Command-line arguments for the packager.
#[derive(Parser, Debug)]
#[command(name = "packager")]
#[command(author, version, about = "Create self-contained installer executables", long_about = None)]
struct Args {
    /// Input directory containing files to package
    #[arg(short, long)]
    input: PathBuf,

    /// Output installer executable path (.exe)
    #[arg(short, long)]
    output: PathBuf,

    /// Path to installer template executable (installer_gui.exe)
    /// If not specified, will look for installer_gui.exe in the same directory as packager
    #[arg(long)]
    template: Option<PathBuf>,

    /// Path to YAML configuration file (default: packager.yaml in input directory)
    #[arg(short, long)]
    config: Option<PathBuf>,

    /// UI resources directory to embed
    #[arg(long)]
    ui_resources: Option<PathBuf>,

    /// Compression level (1-22 for Zstd, 0-9 for LZMA)
    #[arg(long)]
    compression_level: Option<u8>,

    /// Number of threads for parallel compression
    #[arg(long)]
    threads: Option<usize>,

    /// Verbose output
    #[arg(short, long)]
    verbose: bool,

    /// Generate package data file only (legacy mode, not recommended)
    #[arg(long, hide = true)]
    package_only: bool,
}

fn main() -> Result<()> {
    let args = Args::parse();

    // Initialize logging
    init_cli_logging(args.verbose).context("Failed to initialize logging")?;

    info!("Packager CLI v{}", env!("CARGO_PKG_VERSION"));

    // Load configuration
    let config_path = args
        .config
        .clone()
        .unwrap_or_else(|| find_default_config(&args.input));

    if args.config.is_none() {
        let config_dir = args.input.join("config");
        let payload_dir = args.input.join("payload");
        if !config_dir.is_dir() || !payload_dir.is_dir() {
            anyhow::bail!(
                "Input directory must contain both 'config' and 'payload' folders. \
Found config: {}, payload: {}",
                config_dir.exists(),
                payload_dir.exists()
            );
        }
        if !config_path.exists() {
            anyhow::bail!(
                "Missing config file at {:?}. Provide --config to override.",
                config_path
            );
        }
    }
    let mut config = load_config(&config_path, &args)?;

    // Resolve config-relative paths against config file directory.
    let config_base = config_path
        .parent()
        .unwrap_or_else(|| args.input.as_path());

    let payload_root = resolve_payload_root(&args.input, &config_path)?;

    info!("Packaging: {}", config.application_name);
    info!("Version: {}", config.version);
    info!("Input: {:?}", payload_root);
    info!("Output: {:?}", args.output);

    // Create packager
    let packager = Packager::new(config)?;

    // Determine UI resources directory
    let ui_resources_dir_resolved = args.ui_resources.clone().or_else(|| {
        packager
            .config()
            .ui_resources_dir
            .as_ref()
            .map(|path| if path.is_relative() { config_base.join(path) } else { path.clone() })
    });
    let ui_resources_dir = ui_resources_dir_resolved.as_deref();

    let stats = if args.package_only {
        // Legacy mode: generate package data file only
        warn!("Using legacy package-only mode. The output file cannot be run directly.");
        packager.build_package(&payload_root, &args.output, ui_resources_dir, |event| {
            print_progress(&event)
        })?
    } else {
        // Normal mode: generate self-contained installer executable
        let template_exe = find_template_exe(&args)?;
        info!("Using template: {:?}", template_exe);

        packager.build_installer(
            &payload_root,
            &template_exe,
            &args.output,
            ui_resources_dir,
            |event| print_progress(&event),
        )?
    };

    // Print summary
    println!();
    if args.package_only {
        println!("Package data created successfully!");
    } else {
        println!("Installer created successfully!");
        println!("  Output: {:?}", args.output);
    }
    println!("  Files: {}", stats.total_files);
    println!("  Original size: {} bytes", format_size(stats.total_size));
    println!(
        "  Compressed size: {} bytes",
        format_size(stats.compressed_size)
    );
    println!(
        "  Compression ratio: {:.1}%",
        stats.compression_ratio * 100.0
    );

    if !args.package_only {
        println!();
        println!("The installer can be run directly by users:");
        println!("  {:?}", args.output);
    }

    Ok(())
}

/// Find the installer template executable.
fn find_template_exe(args: &Args) -> Result<PathBuf> {
    // 1. Check if explicitly specified
    if let Some(ref template) = args.template {
        if template.exists() {
            return Ok(template.clone());
        }
        anyhow::bail!("Specified template not found: {:?}", template);
    }

    // 2. Look in the same directory as the packager executable
    if let Ok(exe_path) = std::env::current_exe() {
        if let Some(exe_dir) = exe_path.parent() {
            let template_path = exe_dir.join("installer_gui.exe");
            if template_path.exists() {
                return Ok(template_path);
            }

            // Also try installer_gui (without .exe for cross-platform)
            let template_path = exe_dir.join("installer_gui");
            if template_path.exists() {
                return Ok(template_path);
            }
        }
    }

    // 3. Look in current directory
    let current_dir_template = PathBuf::from("installer_gui.exe");
    if current_dir_template.exists() {
        return Ok(current_dir_template);
    }

    // 4. Look in target/release (for development)
    let dev_template = PathBuf::from("target/release/installer_gui.exe");
    if dev_template.exists() {
        return Ok(dev_template);
    }

    anyhow::bail!(
        "Installer template not found. Please specify --template <path> or ensure \
         installer_gui.exe is in the same directory as the packager.\n\
         \n\
         Build the template with: cargo build --release -p installer_gui"
    )
}

/// Load configuration from file or use defaults.
fn load_config(config_path: &PathBuf, args: &Args) -> Result<PackagerConfig> {
    let mut config = if config_path.exists() {
        let ext = config_path
            .extension()
            .and_then(|s| s.to_str())
            .unwrap_or_default()
            .to_ascii_lowercase();
        if ext == "json" {
            anyhow::bail!(
                "JSON config is no longer supported. Please use YAML (e.g. packager.yaml)"
            );
        }
        let content = std::fs::read_to_string(config_path)
            .with_context(|| format!("Failed to read config file: {:?}", config_path))?;
        serde_yaml::from_str(&content)
            .with_context(|| format!("Failed to parse config file: {:?}", config_path))?
    } else {
        info!("No config file found, using defaults");
        PackagerConfig::default()
    };

    // Override with command-line arguments
    if let Some(level) = args.compression_level {
        config.compression_level = level;
    }
    if let Some(threads) = args.threads {
        config.thread_count = Some(threads);
    }
    if args.ui_resources.is_some() {
        config.ui_resources_dir = args.ui_resources.clone();
    }

    Ok(config)
}

fn find_default_config(input_dir: &PathBuf) -> PathBuf {
    let candidates = [
        input_dir.join("config").join("packager.yaml"),
        input_dir.join("packager.yaml"),
    ];
    for path in &candidates {
        if path.exists() {
            return path.clone();
        }
    }
    candidates
        .last()
        .cloned()
        .unwrap_or_else(|| input_dir.join("packager.yaml"))
}

// No config path normalization needed here; installer_core resolves flow/scripts relative to input.

fn resolve_payload_root(input_dir: &PathBuf, config_path: &PathBuf) -> Result<PathBuf> {
    let config_parent = match config_path.parent() {
        Some(parent) => parent,
        None => return Ok(input_dir.clone()),
    };
    let config_root = match config_parent.parent() {
        Some(parent) => parent,
        None => return Ok(input_dir.clone()),
    };

    let config_in_input = config_parent.ends_with("config") && config_root == input_dir.as_path();
    let payload_candidate = input_dir.join("payload");
    if config_in_input && payload_candidate.is_dir() {
        return Ok(payload_candidate);
    }

    Ok(input_dir.clone())
}

/// Print progress to console.
fn print_progress(event: &ProgressEvent) {
    let phase_str = match event.phase {
        Phase::Scanning => "Scanning",
        Phase::Compressing => "Compressing",
        Phase::Decompressing => "Decompressing",
        Phase::Writing => "Writing",
        Phase::Completing => "Completing",
    };

    let percentage = event.percentage();
    let bar_width = 40;
    let filled = (percentage / 100.0 * bar_width as f64) as usize;
    let empty = bar_width - filled;

    print!(
        "\r{:12} [{}{}] {:5.1}%",
        phase_str,
        "=".repeat(filled),
        " ".repeat(empty),
        percentage
    );

    if let Some(ref file) = event.current_file {
        print!(" {}", truncate_path(file, 30));
    }

    use std::io::Write;
    std::io::stdout().flush().ok();
}

/// Truncate a path string for display.
fn truncate_path(path: &str, max_len: usize) -> String {
    if path.len() <= max_len {
        path.to_string()
    } else {
        format!("...{}", &path[path.len() - max_len + 3..])
    }
}

/// Format file size for display.
fn format_size(bytes: u64) -> String {
    if bytes < 1024 {
        format!("{}", bytes)
    } else if bytes < 1024 * 1024 {
        format!("{:.1} KB", bytes as f64 / 1024.0)
    } else if bytes < 1024 * 1024 * 1024 {
        format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
    } else {
        format!("{:.2} GB", bytes as f64 / (1024.0 * 1024.0 * 1024.0))
    }
}
