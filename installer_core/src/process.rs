use installer_shared::{InstallerError, Result};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

pub fn split_command_args(raw: &str) -> Vec<String> {
    let mut args = Vec::new();
    let mut current = String::new();
    let mut in_quotes = false;
    let mut quote_char = '\0';
    let mut escape = false;

    for ch in raw.chars() {
        if escape {
            current.push(ch);
            escape = false;
            continue;
        }
        if ch == '\\' {
            escape = true;
            continue;
        }
        if in_quotes {
            if ch == quote_char {
                in_quotes = false;
            } else {
                current.push(ch);
            }
            continue;
        }
        if ch == '"' || ch == '\'' {
            in_quotes = true;
            quote_char = ch;
            continue;
        }
        if ch.is_whitespace() {
            if !current.is_empty() {
                args.push(current.clone());
                current.clear();
            }
            continue;
        }
        current.push(ch);
    }

    if !current.is_empty() {
        args.push(current);
    }

    args
}

pub fn run_program(program: &str, args: &[String], step_id: &str) -> Result<()> {
    let mut command = std::process::Command::new(program);
    command.args(args);

    #[cfg(windows)]
    {
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        command.creation_flags(CREATE_NO_WINDOW);
    }

    let output = command.output().map_err(|e| {
        InstallerError::Config(format!(
            "Step '{}' failed to start '{}': {}",
            step_id, program, e
        ))
    })?;

    if output.status.success() {
        return Ok(());
    }

    let stderr = String::from_utf8_lossy(&output.stderr);
    Err(InstallerError::Config(format!(
        "Step '{}' command '{}' failed (exit {}): {}",
        step_id,
        program,
        output
            .status
            .code()
            .map(|c| c.to_string())
            .unwrap_or_else(|| "unknown".to_string()),
        stderr.trim()
    )))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn split_command_args_handles_quotes() {
        let args = split_command_args(r#"--name "My App" --flag 'x y'"#);
        assert_eq!(args, vec!["--name", "My App", "--flag", "x y"]);
    }
}
