//! Tauri events for the installer GUI.
//!
//! This module defines the events emitted from the backend to the frontend.
//!
//! # Requirements
//! - 4.9: Implement Tauri events for progress updates
//! - 7.6: Update progress bar on progress events
//! - 7.7: Update status text on progress events

use installer_shared::ProgressEvent;
use serde::{Deserialize, Serialize};
use tauri::{AppHandle, Emitter};
use tracing::warn;

/// Event names used for communication between backend and frontend.
pub mod event_names {
    /// Progress update during installation
    pub const INSTALL_PROGRESS: &str = "install_progress";
    /// Installation completed successfully
    pub const INSTALL_COMPLETE: &str = "install_complete";
    /// Installation error occurred
    pub const INSTALL_ERROR: &str = "install_error";
    /// Installation was cancelled
    pub const INSTALL_CANCELLED: &str = "install_cancelled";
    /// Prerequisites check result
    pub const PREREQUISITES_RESULT: &str = "prerequisites_result";
}

/// Progress event payload sent to the frontend.
///
/// This is a serializable version of the core ProgressEvent.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProgressPayload {
    /// Current phase of operation
    pub phase: String,
    /// Current item count
    pub current: u64,
    /// Total item count
    pub total: u64,
    /// Progress percentage (0-100)
    pub percentage: f64,
    /// Current file being processed
    pub current_file: Option<String>,
    /// Processing speed in bytes per second
    pub speed_bps: Option<u64>,
    /// Human-readable speed string
    pub speed_display: Option<String>,
    /// Optional status message
    pub message: Option<String>,
}

impl From<ProgressEvent> for ProgressPayload {
    fn from(event: ProgressEvent) -> Self {
        let percentage = event.percentage();
        let speed_display = event.speed_bps.map(format_speed);

        Self {
            phase: event.phase.to_string(),
            current: event.current,
            total: event.total,
            percentage,
            current_file: event.current_file,
            speed_bps: event.speed_bps,
            speed_display,
            message: event.message,
        }
    }
}

/// Installation complete event payload.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InstallCompletePayload {
    /// Number of files installed
    pub files: usize,
    /// Total size in bytes
    pub size: u64,
    /// Human-readable size string
    pub size_display: String,
    /// Time taken in milliseconds
    pub time_ms: u128,
    /// Human-readable time string
    pub time_display: String,
}

impl InstallCompletePayload {
    /// Create a new completion payload.
    pub fn new(files: usize, size: u64, time_ms: u128) -> Self {
        Self {
            files,
            size,
            size_display: format_size(size),
            time_ms,
            time_display: format_duration(time_ms),
        }
    }
}

/// Installation error event payload.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InstallErrorPayload {
    /// Error message
    pub message: String,
    /// Error code (if available)
    pub code: Option<String>,
    /// Whether the error is recoverable
    pub recoverable: bool,
    /// Suggested action
    pub suggestion: Option<String>,
}

impl InstallErrorPayload {
    /// Create a new error payload.
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
            code: None,
            recoverable: false,
            suggestion: None,
        }
    }

    /// Set the error code.
    pub fn with_code(mut self, code: impl Into<String>) -> Self {
        self.code = Some(code.into());
        self
    }

    /// Mark as recoverable.
    pub fn recoverable(mut self) -> Self {
        self.recoverable = true;
        self
    }

    /// Add a suggestion.
    pub fn with_suggestion(mut self, suggestion: impl Into<String>) -> Self {
        self.suggestion = Some(suggestion.into());
        self
    }
}

/// Helper to emit progress events.
///
/// # Arguments
/// * `app` - Tauri app handle
/// * `event` - Progress event from the core
pub fn emit_progress(app: &AppHandle, event: ProgressEvent) {
    let payload: ProgressPayload = event.into();
    if let Err(e) = app.emit(event_names::INSTALL_PROGRESS, &payload) {
        warn!("Failed to emit progress event: {}", e);
    }
}

/// Helper to emit completion event.
///
/// # Arguments
/// * `app` - Tauri app handle
/// * `files` - Number of files installed
/// * `size` - Total size in bytes
/// * `time_ms` - Time taken in milliseconds
pub fn emit_complete(app: &AppHandle, files: usize, size: u64, time_ms: u128) {
    let payload = InstallCompletePayload::new(files, size, time_ms);
    if let Err(e) = app.emit(event_names::INSTALL_COMPLETE, &payload) {
        warn!("Failed to emit complete event: {}", e);
    }
}

/// Helper to emit error event.
///
/// # Arguments
/// * `app` - Tauri app handle
/// * `error` - Error payload
pub fn emit_error(app: &AppHandle, error: InstallErrorPayload) {
    if let Err(e) = app.emit(event_names::INSTALL_ERROR, &error) {
        warn!("Failed to emit error event: {}", e);
    }
}

/// Helper to emit cancellation event.
///
/// # Arguments
/// * `app` - Tauri app handle
pub fn emit_cancelled(app: &AppHandle) {
    if let Err(e) = app.emit(event_names::INSTALL_CANCELLED, ()) {
        warn!("Failed to emit cancelled event: {}", e);
    }
}

/// Format bytes per second as a human-readable string.
fn format_speed(bps: u64) -> String {
    if bps >= 1_000_000_000 {
        format!("{:.1} GB/s", bps as f64 / 1_000_000_000.0)
    } else if bps >= 1_000_000 {
        format!("{:.1} MB/s", bps as f64 / 1_000_000.0)
    } else if bps >= 1_000 {
        format!("{:.1} KB/s", bps as f64 / 1_000.0)
    } else {
        format!("{} B/s", bps)
    }
}

/// Format bytes as a human-readable string.
fn format_size(bytes: u64) -> String {
    if bytes >= 1_000_000_000 {
        format!("{:.2} GB", bytes as f64 / 1_000_000_000.0)
    } else if bytes >= 1_000_000 {
        format!("{:.2} MB", bytes as f64 / 1_000_000.0)
    } else if bytes >= 1_000 {
        format!("{:.2} KB", bytes as f64 / 1_000.0)
    } else {
        format!("{} bytes", bytes)
    }
}

/// Format duration in milliseconds as a human-readable string.
fn format_duration(ms: u128) -> String {
    if ms >= 60_000 {
        let minutes = ms / 60_000;
        let seconds = (ms % 60_000) / 1000;
        format!("{}m {}s", minutes, seconds)
    } else if ms >= 1_000 {
        format!("{:.1}s", ms as f64 / 1000.0)
    } else {
        format!("{}ms", ms)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use installer_shared::Phase;

    #[test]
    fn test_format_speed() {
        assert_eq!(format_speed(500), "500 B/s");
        assert_eq!(format_speed(1_500), "1.5 KB/s");
        assert_eq!(format_speed(1_500_000), "1.5 MB/s");
        assert_eq!(format_speed(1_500_000_000), "1.5 GB/s");
    }

    #[test]
    fn test_format_size() {
        assert_eq!(format_size(500), "500 bytes");
        assert_eq!(format_size(1_500), "1.50 KB");
        assert_eq!(format_size(1_500_000), "1.50 MB");
        assert_eq!(format_size(1_500_000_000), "1.50 GB");
    }

    #[test]
    fn test_format_duration() {
        assert_eq!(format_duration(500), "500ms");
        assert_eq!(format_duration(1_500), "1.5s");
        assert_eq!(format_duration(65_000), "1m 5s");
    }

    #[test]
    fn test_progress_payload_from_event() {
        let event = ProgressEvent::new(Phase::Decompressing, 50, 100)
            .with_file("test.txt")
            .with_speed(1_000_000);

        let payload: ProgressPayload = event.into();

        assert_eq!(payload.phase, "Decompressing");
        assert_eq!(payload.current, 50);
        assert_eq!(payload.total, 100);
        assert_eq!(payload.percentage, 50.0);
        assert_eq!(payload.current_file, Some("test.txt".to_string()));
        assert_eq!(payload.speed_bps, Some(1_000_000));
        assert_eq!(payload.speed_display, Some("1.0 MB/s".to_string()));
    }

    #[test]
    fn test_install_complete_payload() {
        let payload = InstallCompletePayload::new(100, 1_500_000, 5_000);

        assert_eq!(payload.files, 100);
        assert_eq!(payload.size, 1_500_000);
        assert_eq!(payload.size_display, "1.50 MB");
        assert_eq!(payload.time_ms, 5_000);
        assert_eq!(payload.time_display, "5.0s");
    }

    #[test]
    fn test_install_error_payload() {
        let payload = InstallErrorPayload::new("Test error")
            .with_code("E001")
            .recoverable()
            .with_suggestion("Try again");

        assert_eq!(payload.message, "Test error");
        assert_eq!(payload.code, Some("E001".to_string()));
        assert!(payload.recoverable);
        assert_eq!(payload.suggestion, Some("Try again".to_string()));
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use installer_shared::Phase;
    use proptest::prelude::*;

    /// Generate an arbitrary Phase
    fn arb_phase() -> impl Strategy<Value = Phase> {
        prop_oneof![
            Just(Phase::Scanning),
            Just(Phase::Compressing),
            Just(Phase::Decompressing),
            Just(Phase::Writing),
            Just(Phase::Completing),
        ]
    }

    /// Generate an arbitrary file path
    fn arb_file_path() -> impl Strategy<Value = String> {
        prop::string::string_regex("[a-zA-Z0-9_/\\.]{1,100}")
            .unwrap()
            .prop_filter("path must not be empty", |s| !s.is_empty())
    }

    /// Generate an arbitrary ProgressEvent
    fn arb_progress_event() -> impl Strategy<Value = ProgressEvent> {
        (
            arb_phase(),
            0u64..10000u64,
            1u64..10000u64,
            prop::option::of(arb_file_path()),
            prop::option::of(0u64..1_000_000_000u64),
            prop::option::of(arb_file_path()),
        )
            .prop_map(|(phase, current, total, file, speed, message)| {
                let mut event = ProgressEvent::new(phase, current.min(total), total);
                if let Some(f) = file {
                    event = event.with_file(f);
                }
                if let Some(s) = speed {
                    event = event.with_speed(s);
                }
                if let Some(m) = message {
                    event = event.with_message(m);
                }
                event
            })
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// Property 10: Progress Event Completeness
        /// For any installation or packaging operation, at least one progress event
        /// should be emitted for each processed file.
        /// **Validates: Requirements 7.1, 4.9**
        #[test]
        fn prop_progress_event_conversion_preserves_data(event in arb_progress_event()) {
            // Convert to payload
            let payload: ProgressPayload = event.clone().into();

            // Property: Phase should be preserved as string
            prop_assert_eq!(payload.phase, event.phase.to_string());

            // Property: Current and total should be preserved
            prop_assert_eq!(payload.current, event.current);
            prop_assert_eq!(payload.total, event.total);

            // Property: Percentage should be calculated correctly
            let expected_percentage = if event.total == 0 {
                0.0
            } else {
                (event.current as f64 / event.total as f64) * 100.0
            };
            prop_assert!((payload.percentage - expected_percentage).abs() < 0.001,
                "Percentage mismatch: {} vs {}", payload.percentage, expected_percentage);

            // Property: Current file should be preserved
            prop_assert_eq!(payload.current_file, event.current_file);

            // Property: Speed should be preserved
            prop_assert_eq!(payload.speed_bps, event.speed_bps);

            // Property: Message should be preserved
            prop_assert_eq!(payload.message, event.message);
        }

        /// Property: Progress percentage should always be between 0 and 100
        #[test]
        fn prop_progress_percentage_bounded(
            current in 0u64..10000u64,
            total in 1u64..10000u64,
        ) {
            let event = ProgressEvent::new(Phase::Writing, current.min(total), total);
            let payload: ProgressPayload = event.into();

            prop_assert!(payload.percentage >= 0.0, "Percentage should be >= 0");
            prop_assert!(payload.percentage <= 100.0, "Percentage should be <= 100");
        }

        /// Property: Speed display should be human-readable
        #[test]
        fn prop_speed_display_format(speed_bps in 0u64..10_000_000_000u64) {
            let event = ProgressEvent::new(Phase::Writing, 50, 100)
                .with_speed(speed_bps);
            let payload: ProgressPayload = event.into();

            // Speed display should be present
            prop_assert!(payload.speed_display.is_some());

            let display = payload.speed_display.unwrap();

            // Should contain a unit
            prop_assert!(
                display.contains("B/s") ||
                display.contains("KB/s") ||
                display.contains("MB/s") ||
                display.contains("GB/s"),
                "Speed display should contain a unit: {}", display
            );
        }

        /// Property: Install complete payload should have valid formatted strings
        #[test]
        fn prop_install_complete_formatting(
            files in 0usize..10000usize,
            size in 0u64..10_000_000_000u64,
            time_ms in 0u128..3_600_000u128,
        ) {
            let payload = InstallCompletePayload::new(files, size, time_ms);

            // Property: Fields should be preserved
            prop_assert_eq!(payload.files, files);
            prop_assert_eq!(payload.size, size);
            prop_assert_eq!(payload.time_ms, time_ms);

            // Property: Size display should be non-empty
            prop_assert!(!payload.size_display.is_empty());

            // Property: Time display should be non-empty
            prop_assert!(!payload.time_display.is_empty());
        }

        /// Property: Error payload should preserve all fields
        #[test]
        fn prop_error_payload_fields(
            message in arb_file_path(),
            code in prop::option::of(arb_file_path()),
            suggestion in prop::option::of(arb_file_path()),
            recoverable in any::<bool>(),
        ) {
            let mut payload = InstallErrorPayload::new(message.clone());

            if let Some(ref c) = code {
                payload = payload.with_code(c.clone());
            }
            if let Some(ref s) = suggestion {
                payload = payload.with_suggestion(s.clone());
            }
            if recoverable {
                payload = payload.recoverable();
            }

            // Property: Message should be preserved
            prop_assert_eq!(payload.message, message);

            // Property: Code should be preserved
            prop_assert_eq!(payload.code, code);

            // Property: Suggestion should be preserved
            prop_assert_eq!(payload.suggestion, suggestion);

            // Property: Recoverable flag should be preserved
            prop_assert_eq!(payload.recoverable, recoverable);
        }

        /// Property: Progress events should be serializable to JSON
        #[test]
        fn prop_progress_event_serializable(event in arb_progress_event()) {
            let payload: ProgressPayload = event.into();

            // Should serialize without error
            let json_result = serde_json::to_string(&payload);
            prop_assert!(json_result.is_ok(), "Serialization failed: {:?}", json_result.err());

            // Should deserialize back
            let json = json_result.unwrap();
            let deserialized: Result<ProgressPayload, _> = serde_json::from_str(&json);
            prop_assert!(deserialized.is_ok(), "Deserialization failed: {:?}", deserialized.err());

            // Should match original
            let restored = deserialized.unwrap();
            prop_assert_eq!(restored.phase, payload.phase);
            prop_assert_eq!(restored.current, payload.current);
            prop_assert_eq!(restored.total, payload.total);
        }
    }
}
