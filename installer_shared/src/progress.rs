//! Progress event types for reporting installation/packaging progress.

use serde::{Deserialize, Serialize};

/// Progress event sent from core to UI.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct ProgressEvent {
    /// Current phase of operation
    pub phase: Phase,
    /// Current item count
    pub current: u64,
    /// Total item count
    pub total: u64,
    /// Current file being processed
    pub current_file: Option<String>,
    /// Processing speed in bytes per second
    pub speed_bps: Option<u64>,
    /// Optional status message
    pub message: Option<String>,
}

impl ProgressEvent {
    /// Create a new progress event.
    pub fn new(phase: Phase, current: u64, total: u64) -> Self {
        Self {
            phase,
            current,
            total,
            current_file: None,
            speed_bps: None,
            message: None,
        }
    }

    /// Set the current file being processed.
    pub fn with_file(mut self, file: impl Into<String>) -> Self {
        self.current_file = Some(file.into());
        self
    }

    /// Set the processing speed.
    pub fn with_speed(mut self, speed_bps: u64) -> Self {
        self.speed_bps = Some(speed_bps);
        self
    }

    /// Set a status message.
    pub fn with_message(mut self, message: impl Into<String>) -> Self {
        self.message = Some(message.into());
        self
    }

    /// Calculate progress percentage (0-100).
    pub fn percentage(&self) -> f64 {
        if self.total == 0 {
            0.0
        } else {
            (self.current as f64 / self.total as f64) * 100.0
        }
    }
}

/// Installation/packaging phase.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum Phase {
    /// Scanning input directory
    Scanning,
    /// Compressing files
    Compressing,
    /// Decompressing files
    Decompressing,
    /// Writing files to disk
    Writing,
    /// Completing installation (registry, shortcuts, etc.)
    Completing,
}

impl std::fmt::Display for Phase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Phase::Scanning => write!(f, "Scanning"),
            Phase::Compressing => write!(f, "Compressing"),
            Phase::Decompressing => write!(f, "Decompressing"),
            Phase::Writing => write!(f, "Writing"),
            Phase::Completing => write!(f, "Completing"),
        }
    }
}
