//! # installer_shared
//!
//! Shared types, error definitions, and configuration models for the installer system.
//!
//! This crate provides the foundational types used by both the packager and installer
//! components. It is designed to be lightweight with minimal dependencies.
//!
//! ## Modules
//!
//! - [`error`] - Error types and Result alias for the installer system
//! - [`mod@format`] - Package format data structures (Header, TOC, Metadata, Footer)
//! - [`config`] - Configuration models for packager and installer
//! - [`progress`] - Progress event types for UI updates
//! - [`logging`] - Logging configuration utilities
//!
//! ## Quick Start
//!
//! ```rust
//! use installer_shared::{PackagerConfig, PackageMetadata, ProgressEvent, Phase};
//!
//! // Create a packager configuration
//! let config = PackagerConfig {
//!     application_name: "MyApp".to_string(),
//!     version: "1.0.0".to_string(),
//!     ..Default::default()
//! };
//!
//! // Create a progress event
//! let progress = ProgressEvent::new(Phase::Compressing, 50, 100)
//!     .with_file("data.bin")
//!     .with_speed(1024 * 1024);
//!
//! println!("Progress: {:.1}%", progress.percentage());
//! ```
//!
//! ## Package Format
//!
//! The package format consists of five sections:
//!
//! ```text
//! ┌─────────────────┐
//! │     Header      │  Magic "MTI2", version, offsets
//! ├─────────────────┤
//! │      TOC        │  File and block entries with CRC32
//! ├─────────────────┤
//! │    Metadata     │  App info (MessagePack encoded)
//! ├─────────────────┤
//! │   Data Blocks   │  Compressed file data
//! ├─────────────────┤
//! │     Footer      │  Magic "MTIF", quick-access offsets
//! └─────────────────┘
//! ```
//!
//! ## Error Handling
//!
//! All fallible operations return [`Result<T>`], which is an alias for
//! `std::result::Result<T, InstallerError>`. The [`InstallerError`] enum
//! covers all possible error conditions.
//!
//! ```rust
//! use installer_shared::{InstallerError, Result};
//!
//! fn check_space(required: u64, available: u64) -> Result<()> {
//!     if available < required {
//!         return Err(InstallerError::InsufficientDiskSpace {
//!             required,
//!             available,
//!         });
//!     }
//!     Ok(())
//! }
//! ```

pub mod error;
pub mod format;
pub mod config;
pub mod logging;
pub mod progress;
pub mod flow;

pub use error::{InstallerError, Result};
pub use flow::*;
pub use format::*;
pub use config::*;
pub use logging::*;
pub use progress::*;
