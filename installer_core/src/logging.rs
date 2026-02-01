//! Logging initialization and configuration for the installer system.
//!
//! This module provides functions to initialize the tracing-based logging system
//! with support for both CLI (stdout) and GUI (file) modes.

use installer_shared::{LogConfig, LogLevel, LogOutput};
use std::fs::File;
use std::io;
use std::path::Path;
use std::sync::Once;
use tracing::Level;
use tracing_subscriber::fmt::time::ChronoLocal;
use tracing_subscriber::prelude::*;
use tracing_subscriber::{fmt, EnvFilter};

static INIT: Once = Once::new();

/// Convert LogLevel to tracing Level.
fn log_level_to_tracing(level: LogLevel) -> Level {
    match level {
        LogLevel::Trace => Level::TRACE,
        LogLevel::Debug => Level::DEBUG,
        LogLevel::Info => Level::INFO,
        LogLevel::Warn => Level::WARN,
        LogLevel::Error => Level::ERROR,
    }
}

/// Initialize the logging system with the given configuration.
///
/// This function should be called once at the start of the application.
/// Subsequent calls will be ignored.
///
/// # Arguments
///
/// * `config` - The logging configuration
///
/// # Returns
///
/// Returns `Ok(())` if initialization was successful, or an error if it failed.
///
/// # Example
///
/// ```no_run
/// use installer_core::logging::init_logging;
/// use installer_shared::{LogConfig, LogLevel};
///
/// // Initialize with default CLI configuration
/// init_logging(&LogConfig::cli()).expect("Failed to initialize logging");
///
/// // Or with custom configuration
/// let config = LogConfig::new()
///     .with_level(LogLevel::Debug)
///     .with_filter("installer_core=debug");
/// init_logging(&config).expect("Failed to initialize logging");
/// ```
pub fn init_logging(config: &LogConfig) -> io::Result<()> {
    let mut result = Ok(());

    INIT.call_once(|| {
        result = init_logging_internal(config);
        if result.is_ok() {
            installer_shared::mark_initialized();
        }
    });

    result
}

fn init_logging_internal(config: &LogConfig) -> io::Result<()> {
    let level = log_level_to_tracing(config.level);

    // Build the filter
    let filter = if let Some(ref filter_str) = config.filter {
        EnvFilter::try_new(filter_str).unwrap_or_else(|_| EnvFilter::new(level.to_string()))
    } else {
        EnvFilter::new(level.to_string())
    };

    match &config.output {
        LogOutput::Stdout => init_stdout_logging(config, filter),
        LogOutput::Stderr => init_stderr_logging(config, filter),
        LogOutput::File(path) => init_file_logging(config, filter, path),
    }
}

fn init_stdout_logging(config: &LogConfig, filter: EnvFilter) -> io::Result<()> {
    let subscriber = tracing_subscriber::registry().with(filter).with(
        fmt::layer()
            .with_writer(io::stdout)
            .with_timer(ChronoLocal::new("%Y-%m-%dT%H:%M:%S%.3f".to_string()))
            .with_target(config.include_target)
            .with_level(config.include_level)
            .with_file(config.include_file_line)
            .with_line_number(config.include_file_line)
            .with_ansi(true),
    );

    tracing::subscriber::set_global_default(subscriber)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))
}

fn init_stderr_logging(config: &LogConfig, filter: EnvFilter) -> io::Result<()> {
    let subscriber = tracing_subscriber::registry().with(filter).with(
        fmt::layer()
            .with_writer(io::stderr)
            .with_timer(ChronoLocal::new("%Y-%m-%dT%H:%M:%S%.3f".to_string()))
            .with_target(config.include_target)
            .with_level(config.include_level)
            .with_file(config.include_file_line)
            .with_line_number(config.include_file_line)
            .with_ansi(true),
    );

    tracing::subscriber::set_global_default(subscriber)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))
}

fn init_file_logging(config: &LogConfig, filter: EnvFilter, path: &Path) -> io::Result<()> {
    // Create parent directories if they don't exist
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }

    // Open the log file
    let file = File::create(path)?;

    let subscriber = tracing_subscriber::registry().with(filter).with(
        fmt::layer()
            .with_writer(file)
            .with_timer(ChronoLocal::new("%Y-%m-%dT%H:%M:%S%.3f".to_string()))
            .with_target(config.include_target)
            .with_level(config.include_level)
            .with_file(config.include_file_line)
            .with_line_number(config.include_file_line)
            .with_ansi(false), // No ANSI colors in file output
    );

    tracing::subscriber::set_global_default(subscriber)
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))
}

/// Initialize CLI-mode logging (stdout).
///
/// This is a convenience function that initializes logging with the default
/// CLI configuration.
///
/// # Arguments
///
/// * `verbose` - If true, use DEBUG level; otherwise use INFO level
pub fn init_cli_logging(verbose: bool) -> io::Result<()> {
    let config = if verbose {
        LogConfig::verbose()
    } else {
        LogConfig::cli()
    };
    init_logging(&config)
}

/// Initialize GUI-mode logging (file in temp directory).
///
/// This is a convenience function that initializes logging with the default
/// GUI configuration, writing logs to a file in the system temp directory.
///
/// # Returns
///
/// Returns the path to the log file on success.
pub fn init_gui_logging() -> io::Result<std::path::PathBuf> {
    let log_path = installer_shared::timestamped_log_path("installer");
    let config = LogConfig::gui().with_output(LogOutput::File(log_path.clone()));
    init_logging(&config)?;
    Ok(log_path)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_level_to_tracing() {
        assert_eq!(log_level_to_tracing(LogLevel::Trace), Level::TRACE);
        assert_eq!(log_level_to_tracing(LogLevel::Debug), Level::DEBUG);
        assert_eq!(log_level_to_tracing(LogLevel::Info), Level::INFO);
        assert_eq!(log_level_to_tracing(LogLevel::Warn), Level::WARN);
        assert_eq!(log_level_to_tracing(LogLevel::Error), Level::ERROR);
    }
}
