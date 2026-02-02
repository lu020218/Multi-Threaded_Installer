//! Flow DSL models and YAML parsing utilities.

use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

use crate::{InstallerError, Result};

const SUPPORTED_FLOW_VERSION: u32 = 1;
const SCRIPT_STEP_TYPE: &str = "script";

/// Top-level flow definition loaded from YAML.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct FlowDefinition {
    /// DSL schema version.
    pub version: u32,
    /// Global variables used by expressions and templates.
    #[serde(default)]
    pub vars: HashMap<String, Value>,
    /// Optional UI flow for page navigation/state.
    #[serde(default)]
    pub ui_flow: Option<UiFlow>,
    /// Installation execution flow.
    pub install_flow: InstallFlow,
}

impl FlowDefinition {
    /// Parse flow definition from YAML string.
    pub fn from_yaml_str(yaml: &str) -> Result<Self> {
        let definition: Self = serde_yaml::from_str(yaml)
            .map_err(|e| InstallerError::Config(format!("Failed to parse YAML flow: {e}")))?;
        definition.validate()?;
        Ok(definition)
    }

    /// Parse flow definition from YAML file.
    pub fn from_yaml_file(path: impl AsRef<Path>) -> Result<Self> {
        let path_ref = path.as_ref();
        let content = fs::read_to_string(path_ref)?;
        Self::from_yaml_str(&content).map_err(|e| match e {
            InstallerError::Config(msg) => {
                InstallerError::Config(format!("Invalid flow file '{}': {msg}", path_ref.display()))
            }
            other => other,
        })
    }

    /// Validate definition schema and semantic constraints.
    pub fn validate(&self) -> Result<()> {
        if self.version != SUPPORTED_FLOW_VERSION {
            return Err(InstallerError::Config(format!(
                "Unsupported flow version {}, expected {}",
                self.version, SUPPORTED_FLOW_VERSION
            )));
        }

        if self.install_flow.steps.is_empty() {
            return Err(InstallerError::Config(
                "install_flow.steps must contain at least one step".to_string(),
            ));
        }

        validate_step_ids(&self.install_flow.steps, "install_flow.steps")?;
        validate_step_ids(&self.install_flow.rollback, "install_flow.rollback")?;

        for step in &self.install_flow.steps {
            validate_step(step, !self.install_flow.rollback.is_empty())?;
        }
        for step in &self.install_flow.rollback {
            validate_step(step, true)?;
        }

        if let Some(ui) = &self.ui_flow {
            validate_ui_flow(ui)?;
        }

        Ok(())
    }
}

/// UI page/event flow.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct UiFlow {
    #[serde(default)]
    pub pages: Vec<UiPage>,
    #[serde(default)]
    pub events: HashMap<String, UiEvent>,
}

/// UI page state node.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct UiPage {
    pub id: String,
    #[serde(default)]
    pub next: Option<String>,
    #[serde(default)]
    pub on_enter: Vec<UiAction>,
}

/// UI page action.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct UiAction {
    #[serde(default)]
    pub set_var: Option<UiSetVarAction>,
}

/// UI set variable action payload.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct UiSetVarAction {
    pub key: String,
    pub from_input: String,
}

/// UI event transition.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct UiEvent {
    #[serde(default)]
    pub goto: Option<String>,
}

/// Install flow section.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct InstallFlow {
    #[serde(default)]
    pub steps: Vec<FlowStep>,
    #[serde(default)]
    pub rollback: Vec<FlowStep>,
}

/// Single execution node in install flow.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct FlowStep {
    pub id: String,
    #[serde(rename = "type")]
    pub step_type: String,
    #[serde(default)]
    pub params: Value,
    #[serde(default)]
    pub when: Option<String>,
    #[serde(default)]
    pub on_fail: Option<OnFailPolicy>,
    #[serde(default)]
    pub engine: Option<ScriptEngine>,
}

impl FlowStep {
    /// Return params as object map (empty map if params is not an object).
    pub fn params_object(&self) -> Map<String, Value> {
        match &self.params {
            Value::Object(map) => map.clone(),
            _ => Map::new(),
        }
    }
}

/// Error handling strategy when a step fails.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum OnFailPolicy {
    Abort,
    Continue,
    Rollback,
}

/// Script engine type for `type: script` nodes.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ScriptEngine {
    Js,
    Ts,
    Wasm,
}

fn validate_step_ids(steps: &[FlowStep], field_name: &str) -> Result<()> {
    let mut ids = HashSet::new();
    for step in steps {
        if step.id.trim().is_empty() {
            return Err(InstallerError::Config(format!(
                "{field_name} contains a step with empty id"
            )));
        }
        if !ids.insert(step.id.clone()) {
            return Err(InstallerError::Config(format!(
                "{field_name} contains duplicate id '{}'",
                step.id
            )));
        }
    }
    Ok(())
}

fn validate_step(step: &FlowStep, has_rollback: bool) -> Result<()> {
    if step.step_type.trim().is_empty() {
        return Err(InstallerError::Config(format!(
            "Step '{}' has empty type",
            step.id
        )));
    }

    if let Some(when) = &step.when {
        validate_when_expression(when, &step.id)?;
    }

    if matches!(step.on_fail, Some(OnFailPolicy::Rollback)) && !has_rollback {
        return Err(InstallerError::Config(format!(
            "Step '{}' uses on_fail=rollback but no rollback flow is defined",
            step.id
        )));
    }

    if step.step_type == SCRIPT_STEP_TYPE {
        if step.engine.is_none() {
            return Err(InstallerError::Config(format!(
                "Script step '{}' is missing engine",
                step.id
            )));
        }
        let params = step.params_object();
        if !matches!(params.get("path"), Some(Value::String(path)) if !path.trim().is_empty()) {
            return Err(InstallerError::Config(format!(
                "Script step '{}' must define params.path",
                step.id
            )));
        }
    } else if step.engine.is_some() {
        return Err(InstallerError::Config(format!(
            "Step '{}' sets engine but is not a script step",
            step.id
        )));
    }

    Ok(())
}

fn validate_ui_flow(ui_flow: &UiFlow) -> Result<()> {
    let mut page_ids = HashSet::new();
    for page in &ui_flow.pages {
        if page.id.trim().is_empty() {
            return Err(InstallerError::Config(
                "ui_flow.pages contains a page with empty id".to_string(),
            ));
        }
        if !page_ids.insert(page.id.clone()) {
            return Err(InstallerError::Config(format!(
                "ui_flow.pages contains duplicate id '{}'",
                page.id
            )));
        }
    }

    for page in &ui_flow.pages {
        if let Some(next) = &page.next {
            if !page_ids.contains(next) {
                return Err(InstallerError::Config(format!(
                    "ui_flow page '{}' points to unknown next page '{}'",
                    page.id, next
                )));
            }
        }
    }

    for (event_name, event) in &ui_flow.events {
        if let Some(target) = &event.goto {
            if !page_ids.contains(target) {
                return Err(InstallerError::Config(format!(
                    "ui_flow event '{}' points to unknown page '{}'",
                    event_name, target
                )));
            }
        }
    }

    Ok(())
}

fn validate_when_expression(expr: &str, step_id: &str) -> Result<()> {
    let trimmed = expr.trim();
    if trimmed.is_empty() {
        return Err(InstallerError::Config(format!(
            "Step '{}' has empty when expression",
            step_id
        )));
    }

    if trimmed == "true" || trimmed == "false" {
        return Ok(());
    }

    if trimmed.starts_with("${") && trimmed.ends_with('}') {
        let inner = &trimmed[2..trimmed.len() - 1];
        if inner.trim().is_empty() {
            return Err(InstallerError::Config(format!(
                "Step '{}' has empty when expression placeholder",
                step_id
            )));
        }
        return validate_when_expression(inner, step_id);
    }

    if trimmed.contains("&&")
        || trimmed.contains("||")
        || trimmed.contains("==")
        || trimmed.contains("!=")
        || trimmed.contains(">=")
        || trimmed.contains("<=")
        || trimmed.contains('>')
        || trimmed.contains('<')
    {
        return Ok(());
    }

    if is_simple_when_atom(trimmed) {
        return Ok(());
    }

    Err(InstallerError::Config(format!(
        "Step '{}' has unsupported when expression '{}'",
        step_id, expr
    )))
}

fn is_simple_when_atom(value: &str) -> bool {
    if value.is_empty() {
        return false;
    }
    if value.starts_with('"') && value.ends_with('"') && value.len() >= 2 {
        return true;
    }
    if value.starts_with('\'') && value.ends_with('\'') && value.len() >= 2 {
        return true;
    }
    if value.parse::<i64>().is_ok() || value.parse::<f64>().is_ok() {
        return true;
    }
    value
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '.')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_and_validate_minimal_flow() {
        let yaml = r#"
version: 1
vars:
  InstallDir: "C:\\Program Files\\Demo"
install_flow:
  steps:
    - id: check_disk
      type: check_disk
      params:
        path: "${InstallDir}"
"#;
        let flow = FlowDefinition::from_yaml_str(yaml).expect("flow should parse");
        assert_eq!(flow.version, 1);
        assert_eq!(flow.install_flow.steps.len(), 1);
    }

    #[test]
    fn reject_script_step_without_engine() {
        let yaml = r#"
version: 1
install_flow:
  steps:
    - id: run_custom
      type: script
      params:
        path: scripts/custom.js
"#;
        let err = FlowDefinition::from_yaml_str(yaml).expect_err("expected validation failure");
        assert!(matches!(err, InstallerError::Config(_)));
    }

    #[test]
    fn reject_unknown_ui_reference() {
        let yaml = r#"
version: 1
ui_flow:
  pages:
    - id: welcome
      next: missing
install_flow:
  steps:
    - id: noop
      type: emit_progress
"#;
        let err = FlowDefinition::from_yaml_str(yaml).expect_err("expected validation failure");
        assert!(matches!(err, InstallerError::Config(_)));
    }

    #[test]
    fn reject_on_fail_rollback_without_rollback_flow() {
        let yaml = r#"
version: 1
install_flow:
  steps:
    - id: extract
      type: extract_package
      on_fail: rollback
"#;
        let err = FlowDefinition::from_yaml_str(yaml).expect_err("expected validation failure");
        assert!(matches!(err, InstallerError::Config(_)));
    }

    #[test]
    fn accept_complex_when_expression() {
        let yaml = r#"
version: 1
install_flow:
  steps:
    - id: conditional
      type: emit_progress
      when: "${options.size_gb >= 2 && (options.desktop_icons == true || vars.ForceInstall == true)}"
"#;
        let flow = FlowDefinition::from_yaml_str(yaml).expect("flow should parse");
        assert_eq!(flow.install_flow.steps.len(), 1);
    }
}
