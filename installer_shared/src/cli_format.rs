//! Shared formatting helpers for CLI output.

/// Truncate long text keeping the tail visible.
pub fn truncate_tail(text: &str, max_len: usize) -> String {
    if text.len() <= max_len {
        text.to_string()
    } else {
        format!("...{}", &text[text.len() - max_len + 3..])
    }
}

/// Format byte size using binary units.
///
/// `decimals` controls decimal precision for KB/MB/GB values.
/// `bytes_with_unit` controls whether bytes are rendered as "N bytes" or "N".
pub fn format_size_with_options(bytes: u64, decimals: usize, bytes_with_unit: bool) -> String {
    const KB: u64 = 1024;
    const MB: u64 = KB * 1024;
    const GB: u64 = MB * 1024;

    if bytes >= GB {
        format!("{:.prec$} GB", bytes as f64 / GB as f64, prec = decimals)
    } else if bytes >= MB {
        format!("{:.prec$} MB", bytes as f64 / MB as f64, prec = decimals)
    } else if bytes >= KB {
        format!("{:.prec$} KB", bytes as f64 / KB as f64, prec = decimals)
    } else if bytes_with_unit {
        format!("{} bytes", bytes)
    } else {
        bytes.to_string()
    }
}

/// Format speed in bytes per second.
pub fn format_speed_bps(bps: u64) -> String {
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
