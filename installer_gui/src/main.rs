//! Installer GUI - Tauri-based graphical installer.
//!
//! This crate provides a modern web-based GUI for the installer using Tauri.
//! Startup orchestration is delegated to `bootstrap` to keep this file minimal.

#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

mod app_builder;
mod bootstrap;
mod commands;
mod custom_ui;
mod embedded_package;
mod events;
mod ui_loader;
mod webview2;

fn main() {
    bootstrap::run();
}
