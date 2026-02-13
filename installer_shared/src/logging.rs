//! Logging system for the installer.
//!
//! This module provides a configurable logging system using the `tracing` crate.
//! It supports:
//! - CLI mode: logs to stdout
//! - GUI mode: logs to a file in the temp directory
//! - Structured logging with timestamps, levels, and module paths
//! - Configurable log levels and filters

use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};

/// Global flag to track if logging has been initialized.
static LOGGING_INITIALIZED: AtomicBool = AtomicBool::new(false);

/// Log level configuration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LogLevel {
    /// Trace level - most verbose
    Trace,
    /// Debug level - detailed debugging information
    Debug,
    /// Info level - general information
    Info,
    /// Warn level - warnings
    Warn,
    /// Error level - errors only
    Error,
}

impl LogLevel {
    /// Get the string representation of the log level.
    pub fn as_str(&self) -> &'static str {
        match self {
            LogLevel::Trace => "TRACE",
            LogLevel::Debug => "DEBUG",
            LogLevel::Info => "INFO",
            LogLevel::Warn => "WARN",
            LogLevel::Error => "ERROR",
        }
    }
}

impl Default for LogLevel {
    fn default() -> Self {
        LogLevel::Info
    }
}

impl std::fmt::Display for LogLevel {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            LogLevel::Trace => write!(f, "TRACE"),
            LogLevel::Debug => write!(f, "DEBUG"),
            LogLevel::Info => write!(f, "INFO"),
            LogLevel::Warn => write!(f, "WARN"),
            LogLevel::Error => write!(f, "ERROR"),
        }
    }
}

impl std::str::FromStr for LogLevel {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_uppercase().as_str() {
            "TRACE" => Ok(LogLevel::Trace),
            "DEBUG" => Ok(LogLevel::Debug),
            "INFO" => Ok(LogLevel::Info),
            "WARN" | "WARNING" => Ok(LogLevel::Warn),
            "ERROR" => Ok(LogLevel::Error),
            _ => Err(format!("Unknown log level: {}", s)),
        }
    }
}

/// Output mode for logging.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum LogOutput {
    /// Log to stdout (CLI mode)
    Stdout,
    /// Log to stderr
    Stderr,
    /// Log to a file (GUI mode)
    File(PathBuf),
}

impl Default for LogOutput {
    fn default() -> Self {
        LogOutput::Stdout
    }
}

/// Configuration for the logging system.
#[derive(Debug, Clone)]
pub struct LogConfig {
    /// Minimum log level to output
    pub level: LogLevel,
    /// Output destination
    pub output: LogOutput,
    /// Whether to include timestamps
    pub include_timestamp: bool,
    /// Whether to include the target (module path)
    pub include_target: bool,
    /// Whether to include the log level
    pub include_level: bool,
    /// Whether to include file and line information
    pub include_file_line: bool,
    /// Optional filter string (e.g., "installer_core=debug,installer_shared=info")
    pub filter: Option<String>,
}

impl Default for LogConfig {
    fn default() -> Self {
        Self {
            level: LogLevel::Info,
            output: LogOutput::Stdout,
            include_timestamp: true,
            include_target: true,
            include_level: true,
            include_file_line: false,
            filter: None,
        }
    }
}

impl LogConfig {
    /// Create a new LogConfig with default settings.
    pub fn new() -> Self {
        Self::default()
    }

    /// Create a CLI-mode configuration (stdout output).
    pub fn cli() -> Self {
        Self {
            level: LogLevel::Info,
            output: LogOutput::Stdout,
            include_timestamp: true,
            include_target: true,
            include_level: true,
            include_file_line: false,
            filter: None,
        }
    }

    /// Create a GUI-mode configuration (file output in temp directory).
    pub fn gui() -> Self {
        let log_path = std::env::temp_dir().join("installer.log");
        Self {
            level: LogLevel::Debug,
            output: LogOutput::File(log_path),
            include_timestamp: true,
            include_target: true,
            include_level: true,
            include_file_line: true,
            filter: None,
        }
    }

    /// Create a verbose configuration for debugging.
    pub fn verbose() -> Self {
        Self {
            level: LogLevel::Debug,
            output: LogOutput::Stdout,
            include_timestamp: true,
            include_target: true,
            include_level: true,
            include_file_line: true,
            filter: None,
        }
    }

    /// Set the log level.
    pub fn with_level(mut self, level: LogLevel) -> Self {
        self.level = level;
        self
    }

    /// Set the output destination.
    pub fn with_output(mut self, output: LogOutput) -> Self {
        self.output = output;
        self
    }

    /// Set the filter string.
    pub fn with_filter(mut self, filter: impl Into<String>) -> Self {
        self.filter = Some(filter.into());
        self
    }

    /// Enable or disable timestamps.
    pub fn with_timestamp(mut self, include: bool) -> Self {
        self.include_timestamp = include;
        self
    }

    /// Enable or disable target (module path).
    pub fn with_target(mut self, include: bool) -> Self {
        self.include_target = include;
        self
    }

    /// Enable or disable file and line information.
    pub fn with_file_line(mut self, include: bool) -> Self {
        self.include_file_line = include;
        self
    }
}

/// Check if logging has been initialized.
pub fn is_initialized() -> bool {
    LOGGING_INITIALIZED.load(Ordering::SeqCst)
}

/// Mark logging as initialized.
pub fn mark_initialized() {
    LOGGING_INITIALIZED.store(true, Ordering::SeqCst);
}

/// Get the default log file path for GUI mode.
pub fn default_log_path() -> PathBuf {
    std::env::temp_dir().join("installer.log")
}

/// Get a timestamped log file path.
pub fn timestamped_log_path(prefix: &str) -> PathBuf {
    let timestamp = chrono::Local::now().format("%Y%m%d_%H%M%S");
    std::env::temp_dir().join(format!("{}_{}.log", prefix, timestamp))
}

/// A structured log entry for testing and serialization.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogEntry {
    /// Timestamp of the log entry
    pub timestamp: Option<String>,
    /// Log level
    pub level: LogLevel,
    /// Target (module path)
    pub target: Option<String>,
    /// Log message
    pub message: String,
    /// File name (if available)
    pub file: Option<String>,
    /// Line number (if available)
    pub line: Option<u32>,
}

impl LogEntry {
    /// Create a new log entry.
    pub fn new(level: LogLevel, message: impl Into<String>) -> Self {
        Self {
            timestamp: None,
            level,
            target: None,
            message: message.into(),
            file: None,
            line: None,
        }
    }

    /// Set the timestamp.
    pub fn with_timestamp(mut self, timestamp: impl Into<String>) -> Self {
        self.timestamp = Some(timestamp.into());
        self
    }

    /// Set the target.
    pub fn with_target(mut self, target: impl Into<String>) -> Self {
        self.target = Some(target.into());
        self
    }

    /// Set file and line information.
    pub fn with_location(mut self, file: impl Into<String>, line: u32) -> Self {
        self.file = Some(file.into());
        self.line = Some(line);
        self
    }

    /// Check if this entry has all required fields for structured logging.
    /// Required fields: timestamp, level, target, message
    pub fn is_complete(&self) -> bool {
        self.timestamp.is_some() && self.target.is_some()
    }

    /// Format the log entry as a string.
    pub fn format(&self) -> String {
        let mut parts = Vec::new();

        if let Some(ref ts) = self.timestamp {
            parts.push(ts.clone());
        }

        parts.push(self.level.to_string());

        if let Some(ref target) = self.target {
            parts.push(target.clone());
        }

        if let (Some(ref file), Some(line)) = (&self.file, self.line) {
            parts.push(format!("{}:{}", file, line));
        }

        parts.push(self.message.clone());

        parts.join(" ")
    }

    /// Parse a log entry from a formatted string.
    /// Expected format: "TIMESTAMP LEVEL TARGET MESSAGE" or "TIMESTAMP LEVEL TARGET FILE:LINE MESSAGE"
    pub fn parse(s: &str) -> Option<Self> {
        let parts: Vec<&str> = s.splitn(4, ' ').collect();
        if parts.len() < 4 {
            return None;
        }

        let timestamp = Some(parts[0].to_string());
        let level = parts[1].parse().ok()?;
        let target = Some(parts[2].to_string());
        let message = parts[3].to_string();

        Some(Self {
            timestamp,
            level,
            target,
            message,
            file: None,
            line: None,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_log_level_display() {
        assert_eq!(LogLevel::Trace.to_string(), "TRACE");
        assert_eq!(LogLevel::Debug.to_string(), "DEBUG");
        assert_eq!(LogLevel::Info.to_string(), "INFO");
        assert_eq!(LogLevel::Warn.to_string(), "WARN");
        assert_eq!(LogLevel::Error.to_string(), "ERROR");
    }

    #[test]
    fn test_log_level_parse() {
        assert_eq!("TRACE".parse::<LogLevel>().unwrap(), LogLevel::Trace);
        assert_eq!("debug".parse::<LogLevel>().unwrap(), LogLevel::Debug);
        assert_eq!("Info".parse::<LogLevel>().unwrap(), LogLevel::Info);
        assert_eq!("WARN".parse::<LogLevel>().unwrap(), LogLevel::Warn);
        assert_eq!("WARNING".parse::<LogLevel>().unwrap(), LogLevel::Warn);
        assert_eq!("error".parse::<LogLevel>().unwrap(), LogLevel::Error);
        assert!("invalid".parse::<LogLevel>().is_err());
    }

    #[test]
    fn test_log_config_defaults() {
        let config = LogConfig::default();
        assert_eq!(config.level, LogLevel::Info);
        assert_eq!(config.output, LogOutput::Stdout);
        assert!(config.include_timestamp);
        assert!(config.include_target);
        assert!(config.include_level);
        assert!(!config.include_file_line);
        assert!(config.filter.is_none());
    }

    #[test]
    fn test_log_config_cli() {
        let config = LogConfig::cli();
        assert_eq!(config.level, LogLevel::Info);
        assert_eq!(config.output, LogOutput::Stdout);
    }

    #[test]
    fn test_log_config_gui() {
        let config = LogConfig::gui();
        assert_eq!(config.level, LogLevel::Debug);
        match config.output {
            LogOutput::File(path) => {
                assert!(path.to_string_lossy().contains("installer.log"));
            }
            _ => panic!("Expected file output"),
        }
    }

    #[test]
    fn test_log_config_builder() {
        let config = LogConfig::new()
            .with_level(LogLevel::Debug)
            .with_output(LogOutput::Stderr)
            .with_filter("installer_core=debug")
            .with_timestamp(false)
            .with_target(false)
            .with_file_line(true);

        assert_eq!(config.level, LogLevel::Debug);
        assert_eq!(config.output, LogOutput::Stderr);
        assert_eq!(config.filter, Some("installer_core=debug".to_string()));
        assert!(!config.include_timestamp);
        assert!(!config.include_target);
        assert!(config.include_file_line);
    }

    #[test]
    fn test_log_entry_new() {
        let entry = LogEntry::new(LogLevel::Info, "Test message");
        assert_eq!(entry.level, LogLevel::Info);
        assert_eq!(entry.message, "Test message");
        assert!(entry.timestamp.is_none());
        assert!(entry.target.is_none());
    }

    #[test]
    fn test_log_entry_builder() {
        let entry = LogEntry::new(LogLevel::Debug, "Debug message")
            .with_timestamp("2024-01-01T12:00:00")
            .with_target("installer_core::packager")
            .with_location("packager.rs", 42);

        assert_eq!(entry.timestamp, Some("2024-01-01T12:00:00".to_string()));
        assert_eq!(entry.target, Some("installer_core::packager".to_string()));
        assert_eq!(entry.file, Some("packager.rs".to_string()));
        assert_eq!(entry.line, Some(42));
    }

    #[test]
    fn test_log_entry_is_complete() {
        let incomplete = LogEntry::new(LogLevel::Info, "Test");
        assert!(!incomplete.is_complete());

        let complete = LogEntry::new(LogLevel::Info, "Test")
            .with_timestamp("2024-01-01T12:00:00")
            .with_target("test::module");
        assert!(complete.is_complete());
    }

    #[test]
    fn test_log_entry_format() {
        let entry = LogEntry::new(LogLevel::Info, "Test message")
            .with_timestamp("2024-01-01T12:00:00")
            .with_target("test::module");

        let formatted = entry.format();
        assert!(formatted.contains("2024-01-01T12:00:00"));
        assert!(formatted.contains("INFO"));
        assert!(formatted.contains("test::module"));
        assert!(formatted.contains("Test message"));
    }

    #[test]
    fn test_default_log_path() {
        let path = default_log_path();
        assert!(path.to_string_lossy().contains("installer.log"));
    }

    #[test]
    fn test_timestamped_log_path() {
        let path = timestamped_log_path("test");
        let path_str = path.to_string_lossy();
        assert!(path_str.contains("test_"));
        assert!(path_str.ends_with(".log"));
    }
}

/// Structured log field for key-value pairs.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LogField {
    /// Field name
    pub key: String,
    /// Field value
    pub value: String,
}

impl LogField {
    /// Create a new log field.
    pub fn new(key: impl Into<String>, value: impl Into<String>) -> Self {
        Self {
            key: key.into(),
            value: value.into(),
        }
    }
}

impl std::fmt::Display for LogField {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}={}", self.key, self.value)
    }
}

/// A builder for creating structured log entries with multiple fields.
#[derive(Debug, Clone)]
pub struct StructuredLogBuilder {
    level: LogLevel,
    message: String,
    fields: Vec<LogField>,
}

impl StructuredLogBuilder {
    /// Create a new structured log builder.
    pub fn new(level: LogLevel, message: impl Into<String>) -> Self {
        Self {
            level,
            message: message.into(),
            fields: Vec::new(),
        }
    }

    /// Add a field to the log entry.
    pub fn field(mut self, key: impl Into<String>, value: impl Into<String>) -> Self {
        self.fields.push(LogField::new(key, value));
        self
    }

    /// Add a numeric field to the log entry.
    pub fn field_num<T: std::fmt::Display>(self, key: impl Into<String>, value: T) -> Self {
        self.field(key, value.to_string())
    }

    /// Add a boolean field to the log entry.
    pub fn field_bool(self, key: impl Into<String>, value: bool) -> Self {
        self.field(key, if value { "true" } else { "false" })
    }

    /// Get the log level.
    pub fn level(&self) -> LogLevel {
        self.level
    }

    /// Get the message.
    pub fn message(&self) -> &str {
        &self.message
    }

    /// Get the fields.
    pub fn fields(&self) -> &[LogField] {
        &self.fields
    }

    /// Format the structured log entry as a string.
    pub fn format(&self) -> String {
        if self.fields.is_empty() {
            self.message.clone()
        } else {
            let fields_str: Vec<String> = self.fields.iter().map(|f| f.to_string()).collect();
            format!("{} {}", self.message, fields_str.join(" "))
        }
    }
}

/// Helper functions for creating structured log builders at different levels.
pub mod structured {
    use super::*;

    /// Create a TRACE level structured log.
    pub fn trace(message: impl Into<String>) -> StructuredLogBuilder {
        StructuredLogBuilder::new(LogLevel::Trace, message)
    }

    /// Create a DEBUG level structured log.
    pub fn debug(message: impl Into<String>) -> StructuredLogBuilder {
        StructuredLogBuilder::new(LogLevel::Debug, message)
    }

    /// Create an INFO level structured log.
    pub fn info(message: impl Into<String>) -> StructuredLogBuilder {
        StructuredLogBuilder::new(LogLevel::Info, message)
    }

    /// Create a WARN level structured log.
    pub fn warn(message: impl Into<String>) -> StructuredLogBuilder {
        StructuredLogBuilder::new(LogLevel::Warn, message)
    }

    /// Create an ERROR level structured log.
    pub fn error(message: impl Into<String>) -> StructuredLogBuilder {
        StructuredLogBuilder::new(LogLevel::Error, message)
    }
}

#[cfg(test)]
mod structured_tests {
    use super::*;

    #[test]
    fn test_log_field() {
        let field = LogField::new("key", "value");
        assert_eq!(field.key, "key");
        assert_eq!(field.value, "value");
        assert_eq!(field.to_string(), "key=value");
    }

    #[test]
    fn test_structured_log_builder() {
        let log = StructuredLogBuilder::new(LogLevel::Info, "Test message")
            .field("file", "test.txt")
            .field_num("size", 1024)
            .field_bool("success", true);

        assert_eq!(log.level(), LogLevel::Info);
        assert_eq!(log.message(), "Test message");
        assert_eq!(log.fields().len(), 3);
    }

    #[test]
    fn test_structured_log_format() {
        let log = structured::info("File processed")
            .field("path", "/tmp/test.txt")
            .field_num("bytes", 4096);

        let formatted = log.format();
        assert!(formatted.contains("File processed"));
        assert!(formatted.contains("path=/tmp/test.txt"));
        assert!(formatted.contains("bytes=4096"));
    }

    #[test]
    fn test_structured_log_empty_fields() {
        let log = structured::debug("Simple message");
        assert_eq!(log.format(), "Simple message");
    }

    #[test]
    fn test_structured_helpers() {
        assert_eq!(structured::trace("msg").level(), LogLevel::Trace);
        assert_eq!(structured::debug("msg").level(), LogLevel::Debug);
        assert_eq!(structured::info("msg").level(), LogLevel::Info);
        assert_eq!(structured::warn("msg").level(), LogLevel::Warn);
        assert_eq!(structured::error("msg").level(), LogLevel::Error);
    }
}

#[cfg(test)]
mod property_tests {
    use super::*;
    use proptest::prelude::*;

    /// Strategy for generating valid log levels.
    fn log_level_strategy() -> impl Strategy<Value = LogLevel> {
        prop_oneof![
            Just(LogLevel::Trace),
            Just(LogLevel::Debug),
            Just(LogLevel::Info),
            Just(LogLevel::Warn),
            Just(LogLevel::Error),
        ]
    }

    /// Strategy for generating valid timestamps.
    fn timestamp_strategy() -> impl Strategy<Value = String> {
        // Generate timestamps in ISO 8601 format
        (
            2020u32..2030,
            1u32..13,
            1u32..29,
            0u32..24,
            0u32..60,
            0u32..60,
        )
            .prop_map(|(year, month, day, hour, min, sec)| {
                format!(
                    "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}",
                    year, month, day, hour, min, sec
                )
            })
    }

    /// Strategy for generating valid module paths (targets).
    fn target_strategy() -> impl Strategy<Value = String> {
        prop::collection::vec("[a-z_][a-z0-9_]*", 1..4).prop_map(|parts| parts.join("::"))
    }

    /// Strategy for generating valid log messages.
    fn message_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z0-9 .,!?:;'-]{1,100}"
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// **Property 17: Log Format Integrity**
        ///
        /// For any log message, the output should contain timestamp, log level,
        /// module path, and message content - all four fields.
        ///
        /// **Validates: Requirements 14.9**
        #[test]
        fn test_log_format_integrity(
            level in log_level_strategy(),
            timestamp in timestamp_strategy(),
            target in target_strategy(),
            message in message_strategy(),
        ) {
            // Create a complete log entry
            let entry = LogEntry::new(level, message.clone())
                .with_timestamp(timestamp.clone())
                .with_target(target.clone());

            // Verify the entry is complete (has all required fields)
            prop_assert!(entry.is_complete(),
                "Log entry should be complete with timestamp and target");

            // Format the entry
            let formatted = entry.format();

            // Verify all four required fields are present in the formatted output
            // 1. Timestamp
            prop_assert!(formatted.contains(&timestamp),
                "Formatted log should contain timestamp: {}", timestamp);

            // 2. Log level
            prop_assert!(formatted.contains(level.as_str()),
                "Formatted log should contain level: {}", level);

            // 3. Module path (target)
            prop_assert!(formatted.contains(&target),
                "Formatted log should contain target: {}", target);

            // 4. Message content
            prop_assert!(formatted.contains(&message),
                "Formatted log should contain message: {}", message);
        }

        /// Property: Log level round-trip through string conversion.
        #[test]
        fn test_log_level_roundtrip(level in log_level_strategy()) {
            let level_str = level.to_string();
            let parsed: LogLevel = level_str.parse().unwrap();
            prop_assert_eq!(level, parsed,
                "Log level should round-trip through string conversion");
        }

        /// Property: Structured log builder preserves all fields.
        #[test]
        fn test_structured_log_preserves_fields(
            level in log_level_strategy(),
            message in message_strategy(),
            field_count in 0usize..5,
        ) {
            let mut builder = StructuredLogBuilder::new(level, message.clone());

            // Add fields
            for i in 0..field_count {
                builder = builder.field(format!("key{}", i), format!("value{}", i));
            }

            // Verify level and message are preserved
            prop_assert_eq!(builder.level(), level);
            prop_assert_eq!(builder.message(), &message);
            prop_assert_eq!(builder.fields().len(), field_count);

            // Verify all fields are present in formatted output
            let formatted = builder.format();
            prop_assert!(formatted.contains(&message));
            for i in 0..field_count {
                let expected = format!("key{}=value{}", i, i);
                prop_assert!(formatted.contains(&expected),
                    "Formatted output should contain field: {}", expected);
            }
        }

        /// Property: LogConfig builder methods are idempotent.
        #[test]
        fn test_log_config_builder_idempotent(
            level in log_level_strategy(),
            include_timestamp in any::<bool>(),
            include_target in any::<bool>(),
            include_file_line in any::<bool>(),
        ) {
            let config = LogConfig::new()
                .with_level(level)
                .with_timestamp(include_timestamp)
                .with_target(include_target)
                .with_file_line(include_file_line);

            prop_assert_eq!(config.level, level);
            prop_assert_eq!(config.include_timestamp, include_timestamp);
            prop_assert_eq!(config.include_target, include_target);
            prop_assert_eq!(config.include_file_line, include_file_line);
        }
    }
}
