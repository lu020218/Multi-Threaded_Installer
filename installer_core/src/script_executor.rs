use crate::flow_executor::FlowContext;
use crate::step_params::StepParams;
use installer_shared::{FlowStep, InstallerError, Result, ScriptEngine};
use serde_json::Value;
use std::path::{Path, PathBuf};

#[cfg(windows)]
use std::os::windows::process::CommandExt;

const PROGRAM_NODE: &str = "node";

/// Script execution safety policy.
#[derive(Debug, Clone, Default)]
pub struct ScriptPolicy {
    /// Whether script nodes are enabled.
    pub enabled: bool,
    /// Canonicalized roots allowed for script loading.
    pub allow_roots: Vec<PathBuf>,
}

impl ScriptPolicy {
    /// Build policy from environment variables.
    ///
    /// - `MTI_ENABLE_SCRIPTS=1|true|yes` enables script execution.
    /// - `MTI_SCRIPT_ALLOWLIST` is a semicolon-separated root list.
    pub fn from_env() -> Self {
        let enabled = std::env::var("MTI_ENABLE_SCRIPTS")
            .ok()
            .map(|v| matches!(v.trim().to_ascii_lowercase().as_str(), "1" | "true" | "yes"))
            .unwrap_or(false);

        let allow_roots = std::env::var("MTI_SCRIPT_ALLOWLIST")
            .ok()
            .map(|v| {
                v.split(';')
                    .filter_map(|entry| {
                        let trimmed = entry.trim();
                        if trimmed.is_empty() {
                            return None;
                        }
                        let path = PathBuf::from(trimmed);
                        canonicalize_fallible(&path)
                    })
                    .collect()
            })
            .unwrap_or_default();

        Self {
            enabled,
            allow_roots,
        }
    }

    /// Create an enabled policy with explicit allow roots.
    pub fn enabled_with_roots(roots: Vec<PathBuf>) -> Self {
        let allow_roots = roots
            .into_iter()
            .filter_map(|p| canonicalize_fallible(&p))
            .collect();
        Self {
            enabled: true,
            allow_roots,
        }
    }

    fn ensure_allowed(&self, script_path: &Path) -> Result<()> {
        if !self.enabled {
            return Err(InstallerError::PermissionDenied(
                "Script execution is disabled. Set MTI_ENABLE_SCRIPTS=1 and allowlist roots."
                    .to_string(),
            ));
        }
        if self.allow_roots.is_empty() {
            return Err(InstallerError::PermissionDenied(
                "Script allowlist is empty. Set MTI_SCRIPT_ALLOWLIST.".to_string(),
            ));
        }

        let canonical = canonicalize_fallible(script_path).ok_or_else(|| {
            InstallerError::Config(format!(
                "Script path '{}' does not exist or cannot be canonicalized",
                script_path.display()
            ))
        })?;

        if self
            .allow_roots
            .iter()
            .any(|root| canonical.starts_with(root))
        {
            Ok(())
        } else {
            Err(InstallerError::PermissionDenied(format!(
                "Script path '{}' is not under allowlisted roots",
                canonical.display()
            )))
        }
    }
}

pub struct ScriptExecutor;

impl ScriptExecutor {
    pub fn execute(
        step: &FlowStep,
        context: &mut FlowContext,
        policy: &ScriptPolicy,
    ) -> Result<()> {
        let params = StepParams::from_step(step);
        let path = params.required_str_with_message(
            "path",
            format!("Script step '{}' missing params.path", step.id),
        )?;

        let (script_path, is_embedded_script) = resolve_script_path(path, context);

        // Embedded scripts materialized from package metadata are treated as trusted package assets.
        // External scripts still require explicit opt-in + allowlist.
        if !is_embedded_script {
            policy.ensure_allowed(&script_path)?;
        }

        let engine = step.engine.ok_or_else(|| {
            InstallerError::Config(format!("Script step '{}' missing engine", step.id))
        })?;
        match engine {
            ScriptEngine::Js => {}
            _ => {
                return Err(InstallerError::Config(format!(
                    "Script engine '{:?}' is not implemented yet",
                    engine
                )))
            }
        }

        let absolute_script = canonicalize_fallible(&script_path).ok_or_else(|| {
            InstallerError::Config(format!(
                "Script path '{}' does not exist",
                script_path.display()
            ))
        })?;

        let args_json = params.value_or_null("args").to_string();
        let vars_json = Value::Object(context.vars.clone().into_iter().collect()).to_string();
        let metadata_json = context.metadata.to_string();
        let options_json = context.options.to_string();

        let mut command = std::process::Command::new(PROGRAM_NODE);
        command
            .arg(&absolute_script)
            .env("MTI_ARGS_JSON", args_json)
            .env("MTI_VARS_JSON", vars_json)
            .env("MTI_METADATA_JSON", metadata_json)
            .env("MTI_OPTIONS_JSON", options_json);

        #[cfg(windows)]
        {
            // Prevent transient console window flashes when launching node.exe from GUI installer.
            const CREATE_NO_WINDOW: u32 = 0x0800_0000;
            command.creation_flags(CREATE_NO_WINDOW);
        }

        let output = command.output().map_err(|e| {
            InstallerError::Config(format!(
                "Script step '{}' failed to start Node.js: {}",
                step.id, e
            ))
        })?;

        if !output.status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr);
            return Err(InstallerError::Config(format!(
                "Script step '{}' failed (exit {}): {}",
                step.id,
                output
                    .status
                    .code()
                    .map(|c| c.to_string())
                    .unwrap_or_else(|| "unknown".to_string()),
                stderr.trim()
            )));
        }

        Ok(())
    }
}

fn resolve_script_path(path: &str, context: &FlowContext) -> (PathBuf, bool) {
    let mut script_path = PathBuf::from(path);
    let mut is_embedded_script = false;

    if !script_path.is_absolute() && !script_path.exists() {
        if let Some(root) = context
            .vars
            .get("__embedded_scripts_root")
            .and_then(Value::as_str)
        {
            let candidate = PathBuf::from(root).join(path);
            if candidate.exists() {
                script_path = candidate;
                is_embedded_script = true;
            }
        }
    }

    (script_path, is_embedded_script)
}

fn canonicalize_fallible(path: &Path) -> Option<PathBuf> {
    let absolute = if path.is_absolute() {
        path.to_path_buf()
    } else {
        std::env::current_dir().ok()?.join(path)
    };
    absolute.canonicalize().ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use installer_shared::{FlowStep, ScriptEngine};
    use serde_json::json;
    use std::collections::HashMap;
    use std::fs;
    use tempfile::tempdir;

    fn base_step() -> FlowStep {
        FlowStep {
            id: "script_step".to_string(),
            step_type: "script".to_string(),
            params: json!({}),
            when: None,
            on_fail: None,
            engine: Some(ScriptEngine::Js),
        }
    }

    #[test]
    fn disabled_policy_rejects_external_script() {
        let dir = tempdir().expect("tempdir");
        let script = dir.path().join("a.js");
        fs::write(&script, "console.log('ok')").expect("write script");

        let mut step = base_step();
        step.params = json!({ "path": script.to_string_lossy().to_string() });

        let mut context = FlowContext::new(HashMap::new(), json!({}), json!({}));
        let policy = ScriptPolicy::default();

        let err = ScriptExecutor::execute(&step, &mut context, &policy)
            .expect_err("disabled policy should fail");
        assert!(matches!(err, InstallerError::PermissionDenied(_)));
    }

    #[test]
    fn non_allowlisted_script_rejected() {
        let allow_dir = tempdir().expect("allow dir");
        let script_dir = tempdir().expect("script dir");
        let script = script_dir.path().join("b.js");
        fs::write(&script, "console.log('ok')").expect("write script");

        let mut step = base_step();
        step.params = json!({ "path": script.to_string_lossy().to_string() });

        let mut context = FlowContext::new(HashMap::new(), json!({}), json!({}));
        let policy = ScriptPolicy::enabled_with_roots(vec![allow_dir.path().to_path_buf()]);

        let err = ScriptExecutor::execute(&step, &mut context, &policy)
            .expect_err("non allowlisted path should fail");
        assert!(matches!(err, InstallerError::PermissionDenied(_)));
    }

    #[test]
    fn non_js_engine_returns_not_implemented() {
        let dir = tempdir().expect("tempdir");
        let script = dir.path().join("c.js");
        fs::write(&script, "console.log('ok')").expect("write script");

        let mut step = base_step();
        step.engine = Some(ScriptEngine::Ts);
        step.params = json!({ "path": script.to_string_lossy().to_string() });

        let mut context = FlowContext::new(HashMap::new(), json!({}), json!({}));
        let policy = ScriptPolicy::enabled_with_roots(vec![dir.path().to_path_buf()]);

        let err = ScriptExecutor::execute(&step, &mut context, &policy)
            .expect_err("non-js engine should fail");
        match err {
            InstallerError::Config(msg) => {
                assert!(msg.contains("not implemented"));
            }
            other => panic!("unexpected error: {other}"),
        }
    }
}
