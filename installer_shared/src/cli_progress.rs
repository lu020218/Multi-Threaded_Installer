//! Shared progress printer for CLI applications.

use std::io::Write;
use std::sync::Mutex;

use crate::{format_speed_bps, truncate_tail, Phase, ProgressEvent};

#[derive(Clone, Copy)]
pub struct CliProgressStyle {
    pub phase_label: fn(Phase) -> &'static str,
    pub show_speed: bool,
    pub bar_width: usize,
    pub file_max_len: usize,
    pub message_max_len: usize,
}

pub struct CliProgressPrinter {
    silent: bool,
    style: CliProgressStyle,
    last_phase: Mutex<Option<Phase>>,
}

impl CliProgressPrinter {
    pub fn new(silent: bool, style: CliProgressStyle) -> Self {
        Self {
            silent,
            style,
            last_phase: Mutex::new(None),
        }
    }

    pub fn print(&self, event: &ProgressEvent) {
        if self.silent {
            return;
        }

        let mut last_phase = self.last_phase.lock().expect("progress lock poisoned");
        let phase_changed = *last_phase != Some(event.phase);
        if phase_changed {
            if last_phase.is_some() {
                println!();
            }
            *last_phase = Some(event.phase);
        }
        drop(last_phase);

        let phase_str = (self.style.phase_label)(event.phase);
        let percentage = event.percentage();
        let filled = (percentage / 100.0 * self.style.bar_width as f64) as usize;
        let empty = self.style.bar_width.saturating_sub(filled);

        let mut line = format!(
            "\r{:12} [{}{}] {:5.1}%",
            phase_str,
            "=".repeat(filled),
            " ".repeat(empty),
            percentage
        );

        if let Some(ref file) = event.current_file {
            line.push_str(&format!(
                " {}",
                truncate_tail(file, self.style.file_max_len)
            ));
        }

        if self.style.show_speed {
            if let Some(speed) = event.speed_bps {
                line.push_str(&format!(" ({})", format_speed_bps(speed)));
            }
        }

        if event.current_file.is_none() {
            if let Some(ref message) = event.message {
                line.push_str(&format!(
                    " {}",
                    truncate_tail(message, self.style.message_max_len)
                ));
            }
        }

        print!("{}\x1b[K", line);
        std::io::stdout().flush().ok();
    }

    pub fn finish_line(&self) {
        if !self.silent {
            println!();
        }
    }

    pub fn is_silent(&self) -> bool {
        self.silent
    }

    pub fn last_phase(&self) -> Option<Phase> {
        self.last_phase
            .lock()
            .expect("progress lock poisoned")
            .clone()
    }
}
