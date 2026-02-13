use installer_shared::{FlowStep, InstallerError, Result};
use serde_json::{Map, Value};

/// Typed accessors for `FlowStep.params`.
pub struct StepParams<'a> {
    step: &'a FlowStep,
    map: Option<&'a Map<String, Value>>,
}

impl<'a> StepParams<'a> {
    pub fn from_step(step: &'a FlowStep) -> Self {
        Self {
            step,
            map: step.params.as_object(),
        }
    }

    pub fn str(&self, key: &str) -> Option<&'a str> {
        self.map
            .and_then(|m| m.get(key))
            .and_then(Value::as_str)
            .map(str::trim)
            .filter(|v| !v.is_empty())
    }

    pub fn bool_or(&self, key: &str, default: bool) -> bool {
        self.map
            .and_then(|m| m.get(key))
            .and_then(Value::as_bool)
            .unwrap_or(default)
    }

    pub fn string_array(&self, key: &str) -> Option<Vec<String>> {
        self.map
            .and_then(|m| m.get(key))
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(str::trim)
                    .filter(|v| !v.is_empty())
                    .map(str::to_string)
                    .collect::<Vec<_>>()
            })
            .filter(|items| !items.is_empty())
    }

    pub fn value_or_null(&self, key: &str) -> Value {
        self.map
            .and_then(|m| m.get(key))
            .cloned()
            .unwrap_or(Value::Null)
    }

    pub fn required_str_for_step(&self, key: &str, step_type: &str) -> Result<&'a str> {
        self.str(key).ok_or_else(|| {
            InstallerError::Config(format!(
                "Step '{}' requires params.{} for {}",
                self.step.id, key, step_type
            ))
        })
    }

    pub fn required_str_with_message(
        &self,
        key: &str,
        message: impl Into<String>,
    ) -> Result<&'a str> {
        self.str(key)
            .ok_or_else(|| InstallerError::Config(message.into()))
    }
}
