//! Flow DSL executor with conditional steps and rollback support.

use std::collections::HashMap;

use serde_json::{Map, Number, Value};

use installer_shared::{FlowDefinition, FlowStep, InstallerError, OnFailPolicy, Result};

const SCRIPT_STEP_TYPE: &str = "script";

/// Mutable execution state shared across all flow steps.
#[derive(Debug, Clone)]
pub struct FlowContext {
    /// Runtime variables.
    pub vars: HashMap<String, Value>,
    /// Package metadata scope.
    pub metadata: Value,
    /// User-selected options scope.
    pub options: Value,
}

impl FlowContext {
    /// Create a context from flow defaults with empty metadata/options.
    pub fn from_definition(definition: &FlowDefinition) -> Self {
        Self {
            vars: definition.vars.clone(),
            metadata: Value::Object(Map::new()),
            options: Value::Object(Map::new()),
        }
    }

    /// Create context from explicit scopes.
    pub fn new(vars: HashMap<String, Value>, metadata: Value, options: Value) -> Self {
        Self {
            vars,
            metadata,
            options,
        }
    }

    /// Set or replace a runtime variable.
    pub fn set_var(&mut self, key: impl Into<String>, value: Value) {
        self.vars.insert(key.into(), value);
    }

    fn resolve_path(&self, path: &str) -> Option<Value> {
        let path = path.trim();
        if path.is_empty() {
            return None;
        }

        let mut segments = path.split('.');
        let first = segments.next()?;

        let mut current = match first {
            "vars" => Value::Object(self.vars.clone().into_iter().collect()),
            "metadata" => self.metadata.clone(),
            "options" => self.options.clone(),
            name => self.vars.get(name).cloned()?,
        };

        for segment in segments {
            match current {
                Value::Object(map) => {
                    current = map.get(segment)?.clone();
                }
                _ => return None,
            }
        }

        Some(current)
    }
}

/// Runtime hooks called by the generic flow executor.
pub trait FlowRuntime {
    /// Execute a built-in step node.
    fn execute_builtin(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()>;
    /// Execute a script step node.
    fn execute_script(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()>;
}

/// Execution summary for diagnostics/tests.
#[derive(Debug, Clone, Default)]
pub struct ExecutionReport {
    pub executed_steps: Vec<String>,
    pub skipped_steps: Vec<String>,
    pub rollback_steps: Vec<String>,
    pub continued_errors: Vec<String>,
}

/// Install flow executor.
#[derive(Debug, Clone)]
pub struct FlowExecutor {
    definition: FlowDefinition,
}

impl FlowExecutor {
    /// Build executor from validated flow definition.
    pub fn new(definition: FlowDefinition) -> Result<Self> {
        definition.validate()?;
        Ok(Self { definition })
    }

    /// Execute install_flow using the provided runtime.
    pub fn execute<R: FlowRuntime>(
        &self,
        runtime: &mut R,
        context: &mut FlowContext,
    ) -> Result<ExecutionReport> {
        let mut report = ExecutionReport::default();

        for step in &self.definition.install_flow.steps {
            if !evaluate_condition(step.when.as_deref(), context)? {
                report.skipped_steps.push(step.id.clone());
                continue;
            }

            let resolved_step = resolve_step(step, context);

            match execute_step(runtime, &resolved_step, context) {
                Ok(()) => report.executed_steps.push(step.id.clone()),
                Err(error) => {
                    let policy = step.on_fail.unwrap_or(OnFailPolicy::Abort);
                    match policy {
                        OnFailPolicy::Abort => return Err(error),
                        OnFailPolicy::Continue => {
                            report
                                .continued_errors
                                .push(format!("{}: {}", step.id, error));
                            report.executed_steps.push(step.id.clone());
                        }
                        OnFailPolicy::Rollback => {
                            self.execute_rollback(runtime, context, &mut report)?;
                            return Err(error);
                        }
                    }
                }
            }
        }

        Ok(report)
    }

    fn execute_rollback<R: FlowRuntime>(
        &self,
        runtime: &mut R,
        context: &mut FlowContext,
        report: &mut ExecutionReport,
    ) -> Result<()> {
        for step in self.definition.install_flow.rollback.iter().rev() {
            let resolved_step = resolve_step(step, context);
            execute_step(runtime, &resolved_step, context).map_err(|e| {
                InstallerError::Rollback(format!("Rollback step '{}' failed: {e}", step.id))
            })?;
            report.rollback_steps.push(step.id.clone());
        }
        Ok(())
    }
}

fn execute_step<R: FlowRuntime>(
    runtime: &mut R,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    if step.step_type == SCRIPT_STEP_TYPE {
        runtime.execute_script(step, context)
    } else {
        runtime.execute_builtin(step, context)
    }
}

fn evaluate_condition(condition: Option<&str>, context: &FlowContext) -> Result<bool> {
    let Some(raw) = condition else {
        return Ok(true);
    };
    let expression = raw.trim();
    if expression.is_empty() {
        return Ok(true);
    }
    if expression == "true" {
        return Ok(true);
    }
    if expression == "false" {
        return Ok(false);
    }

    if expression.starts_with("${") && expression.ends_with('}') {
        let inner = expression[2..expression.len() - 1].trim();
        return eval_expr(inner, context);
    }

    eval_expr(expression, context)
}

fn eval_expr(expression: &str, context: &FlowContext) -> Result<bool> {
    let tokens = tokenize_expression(expression)?;
    let mut parser = ExprParser::new(tokens, context);
    let value = parser.parse_or()?;
    parser.expect_end()?;
    Ok(is_truthy(&value))
}

fn resolve_token(token: &str, context: &FlowContext) -> Result<Value> {
    let trimmed = token.trim();
    if trimmed.is_empty() {
        return Err(InstallerError::Config(
            "Invalid empty token in expression".to_string(),
        ));
    }

    if (trimmed.starts_with('"') && trimmed.ends_with('"'))
        || (trimmed.starts_with('\'') && trimmed.ends_with('\''))
    {
        return Ok(Value::String(trimmed[1..trimmed.len() - 1].to_string()));
    }

    if trimmed == "true" {
        return Ok(Value::Bool(true));
    }
    if trimmed == "false" {
        return Ok(Value::Bool(false));
    }
    if let Ok(v) = trimmed.parse::<i64>() {
        return Ok(Value::Number(Number::from(v)));
    }
    if let Ok(v) = trimmed.parse::<f64>() {
        if let Some(num) = Number::from_f64(v) {
            return Ok(Value::Number(num));
        }
    }

    context
        .resolve_path(trimmed)
        .ok_or_else(|| InstallerError::Config(format!("Unknown expression variable '{}'", trimmed)))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum CompareOp {
    Eq,
    Ne,
    Gt,
    Ge,
    Lt,
    Le,
}

#[derive(Debug, Clone, PartialEq)]
enum ExprToken {
    Atom(String),
    And,
    Or,
    LParen,
    RParen,
    Op(CompareOp),
    End,
}

struct ExprParser<'a> {
    tokens: Vec<ExprToken>,
    index: usize,
    context: &'a FlowContext,
}

impl<'a> ExprParser<'a> {
    fn new(tokens: Vec<ExprToken>, context: &'a FlowContext) -> Self {
        Self {
            tokens,
            index: 0,
            context,
        }
    }

    fn parse_or(&mut self) -> Result<Value> {
        let mut left = self.parse_and()?;
        while self.consume_if(&ExprToken::Or) {
            let right = self.parse_and()?;
            left = Value::Bool(is_truthy(&left) || is_truthy(&right));
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Value> {
        let mut left = self.parse_comparison()?;
        while self.consume_if(&ExprToken::And) {
            let right = self.parse_comparison()?;
            left = Value::Bool(is_truthy(&left) && is_truthy(&right));
        }
        Ok(left)
    }

    fn parse_comparison(&mut self) -> Result<Value> {
        let mut left = self.parse_primary()?;
        while let ExprToken::Op(op) = self.current() {
            let op = *op;
            self.advance();
            let right = self.parse_primary()?;
            left = Value::Bool(compare_values(&left, &right, op)?);
        }
        Ok(left)
    }

    fn parse_primary(&mut self) -> Result<Value> {
        match self.current().clone() {
            ExprToken::LParen => {
                self.advance();
                let value = self.parse_or()?;
                self.expect(&ExprToken::RParen)?;
                Ok(value)
            }
            ExprToken::Atom(text) => {
                self.advance();
                resolve_token(&text, self.context)
            }
            other => Err(InstallerError::Config(format!(
                "Unexpected token in expression: {:?}",
                other
            ))),
        }
    }

    fn expect_end(&self) -> Result<()> {
        if matches!(self.current(), ExprToken::End) {
            Ok(())
        } else {
            Err(InstallerError::Config(
                "Unexpected trailing tokens in expression".to_string(),
            ))
        }
    }

    fn expect(&mut self, token: &ExprToken) -> Result<()> {
        if self.current() == token {
            self.advance();
            Ok(())
        } else {
            Err(InstallerError::Config(format!(
                "Expected token {:?}, got {:?}",
                token,
                self.current()
            )))
        }
    }

    fn consume_if(&mut self, token: &ExprToken) -> bool {
        if self.current() == token {
            self.advance();
            true
        } else {
            false
        }
    }

    fn current(&self) -> &ExprToken {
        self.tokens.get(self.index).unwrap_or(&ExprToken::End)
    }

    fn advance(&mut self) {
        if self.index < self.tokens.len() {
            self.index += 1;
        }
    }
}

fn tokenize_expression(expression: &str) -> Result<Vec<ExprToken>> {
    let chars: Vec<char> = expression.chars().collect();
    let mut i = 0;
    let mut tokens = Vec::new();

    while i < chars.len() {
        let ch = chars[i];
        if ch.is_whitespace() {
            i += 1;
            continue;
        }

        if i + 1 < chars.len() {
            let pair = [chars[i], chars[i + 1]];
            match pair {
                ['&', '&'] => {
                    tokens.push(ExprToken::And);
                    i += 2;
                    continue;
                }
                ['|', '|'] => {
                    tokens.push(ExprToken::Or);
                    i += 2;
                    continue;
                }
                ['=', '='] => {
                    tokens.push(ExprToken::Op(CompareOp::Eq));
                    i += 2;
                    continue;
                }
                ['!', '='] => {
                    tokens.push(ExprToken::Op(CompareOp::Ne));
                    i += 2;
                    continue;
                }
                ['>', '='] => {
                    tokens.push(ExprToken::Op(CompareOp::Ge));
                    i += 2;
                    continue;
                }
                ['<', '='] => {
                    tokens.push(ExprToken::Op(CompareOp::Le));
                    i += 2;
                    continue;
                }
                _ => {}
            }
        }

        match ch {
            '(' => {
                tokens.push(ExprToken::LParen);
                i += 1;
            }
            ')' => {
                tokens.push(ExprToken::RParen);
                i += 1;
            }
            '>' => {
                tokens.push(ExprToken::Op(CompareOp::Gt));
                i += 1;
            }
            '<' => {
                tokens.push(ExprToken::Op(CompareOp::Lt));
                i += 1;
            }
            '"' | '\'' => {
                let quote = ch;
                let mut j = i + 1;
                let mut escaped = false;
                while j < chars.len() {
                    if escaped {
                        escaped = false;
                    } else if chars[j] == '\\' {
                        escaped = true;
                    } else if chars[j] == quote {
                        break;
                    }
                    j += 1;
                }
                if j >= chars.len() || chars[j] != quote {
                    return Err(InstallerError::Config(
                        "Unterminated string literal in expression".to_string(),
                    ));
                }
                let atom: String = chars[i..=j].iter().collect();
                tokens.push(ExprToken::Atom(atom));
                i = j + 1;
            }
            _ => {
                let mut j = i;
                while j < chars.len() {
                    let c = chars[j];
                    if c.is_whitespace()
                        || c == '('
                        || c == ')'
                        || c == '&'
                        || c == '|'
                        || c == '='
                        || c == '!'
                        || c == '>'
                        || c == '<'
                    {
                        break;
                    }
                    j += 1;
                }
                if j == i {
                    return Err(InstallerError::Config(format!(
                        "Invalid token '{}' in expression",
                        ch
                    )));
                }
                let atom: String = chars[i..j].iter().collect();
                tokens.push(ExprToken::Atom(atom));
                i = j;
            }
        }
    }

    tokens.push(ExprToken::End);
    Ok(tokens)
}

fn compare_values(left: &Value, right: &Value, op: CompareOp) -> Result<bool> {
    if matches!(op, CompareOp::Eq) {
        return Ok(left == right);
    }
    if matches!(op, CompareOp::Ne) {
        return Ok(left != right);
    }

    if let (Some(l), Some(r)) = (as_f64(left), as_f64(right)) {
        return Ok(match op {
            CompareOp::Gt => l > r,
            CompareOp::Ge => l >= r,
            CompareOp::Lt => l < r,
            CompareOp::Le => l <= r,
            CompareOp::Eq | CompareOp::Ne => unreachable!(),
        });
    }

    if let (Value::String(l), Value::String(r)) = (left, right) {
        return Ok(match op {
            CompareOp::Gt => l > r,
            CompareOp::Ge => l >= r,
            CompareOp::Lt => l < r,
            CompareOp::Le => l <= r,
            CompareOp::Eq | CompareOp::Ne => unreachable!(),
        });
    }

    if let (Value::Bool(l), Value::Bool(r)) = (left, right) {
        return Ok(match op {
            CompareOp::Gt => l > r,
            CompareOp::Ge => l >= r,
            CompareOp::Lt => l < r,
            CompareOp::Le => l <= r,
            CompareOp::Eq | CompareOp::Ne => unreachable!(),
        });
    }

    Err(InstallerError::Config(format!(
        "Cannot compare values '{}' and '{}' with {:?}",
        left, right, op
    )))
}

fn as_f64(value: &Value) -> Option<f64> {
    match value {
        Value::Number(num) => num.as_f64(),
        _ => None,
    }
}

fn is_truthy(value: &Value) -> bool {
    match value {
        Value::Null => false,
        Value::Bool(v) => *v,
        Value::Number(n) => {
            if let Some(v) = n.as_i64() {
                v != 0
            } else if let Some(v) = n.as_u64() {
                v != 0
            } else {
                n.as_f64().unwrap_or(0.0) != 0.0
            }
        }
        Value::String(s) => !s.trim().is_empty(),
        Value::Array(v) => !v.is_empty(),
        Value::Object(m) => !m.is_empty(),
    }
}

fn resolve_step(step: &FlowStep, context: &FlowContext) -> FlowStep {
    let mut resolved = step.clone();
    resolved.params = resolve_value(&step.params, context);
    resolved
}

fn resolve_value(value: &Value, context: &FlowContext) -> Value {
    match value {
        Value::String(text) => resolve_string(text, context),
        Value::Array(items) => Value::Array(
            items
                .iter()
                .map(|item| resolve_value(item, context))
                .collect(),
        ),
        Value::Object(map) => Value::Object(
            map.iter()
                .map(|(k, v)| (k.clone(), resolve_value(v, context)))
                .collect(),
        ),
        other => other.clone(),
    }
}

fn resolve_string(text: &str, context: &FlowContext) -> Value {
    if let Some(path) = full_placeholder(text) {
        if let Some(value) = context.resolve_path(path) {
            return value;
        }
    }

    let mut output = String::with_capacity(text.len());
    let mut rest = text;
    while let Some(start) = rest.find("${") {
        output.push_str(&rest[..start]);
        let after_start = &rest[start + 2..];
        if let Some(end) = after_start.find('}') {
            let key = after_start[..end].trim();
            if let Some(value) = context.resolve_path(key) {
                output.push_str(&value_to_string(&value));
            } else {
                output.push_str("${");
                output.push_str(key);
                output.push('}');
            }
            rest = &after_start[end + 1..];
        } else {
            output.push_str(&rest[start..]);
            rest = "";
            break;
        }
    }
    output.push_str(rest);
    Value::String(output)
}

fn full_placeholder(text: &str) -> Option<&str> {
    let trimmed = text.trim();
    if !trimmed.starts_with("${") || !trimmed.ends_with('}') {
        return None;
    }
    let inner = &trimmed[2..trimmed.len() - 1];
    if inner.trim().is_empty() {
        None
    } else {
        Some(inner.trim())
    }
}

fn value_to_string(value: &Value) -> String {
    match value {
        Value::Null => String::new(),
        Value::Bool(v) => v.to_string(),
        Value::Number(v) => v.to_string(),
        Value::String(v) => v.clone(),
        Value::Array(_) | Value::Object(_) => value.to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MockRuntime {
        called: Vec<String>,
        fail_on: Option<String>,
    }

    impl MockRuntime {
        fn new(fail_on: Option<&str>) -> Self {
            Self {
                called: Vec::new(),
                fail_on: fail_on.map(ToOwned::to_owned),
            }
        }

        fn call(&mut self, step: &FlowStep) -> Result<()> {
            self.called.push(step.id.clone());
            if self.fail_on.as_deref() == Some(step.id.as_str()) {
                Err(InstallerError::Platform(format!(
                    "forced failure on {}",
                    step.id
                )))
            } else {
                Ok(())
            }
        }
    }

    impl FlowRuntime for MockRuntime {
        fn execute_builtin(&mut self, step: &FlowStep, _context: &mut FlowContext) -> Result<()> {
            self.call(step)
        }

        fn execute_script(&mut self, step: &FlowStep, _context: &mut FlowContext) -> Result<()> {
            self.call(step)
        }
    }

    fn flow_with_steps(step_yaml: &str, rollback_yaml: &str) -> FlowDefinition {
        let yaml = format!(
            r#"
version: 1
vars:
  InstallDir: C:\Demo
install_flow:
  steps:
{step_yaml}
  rollback:
{rollback_yaml}
"#
        );
        FlowDefinition::from_yaml_str(&yaml).expect("flow should parse")
    }

    #[test]
    fn executes_and_rolls_back_on_failure() {
        let flow = flow_with_steps(
            r#"    - id: extract
      type: extract_package
      on_fail: rollback"#,
            r#"    - id: cleanup
      type: rollback_files"#,
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        let mut runtime = MockRuntime::new(Some("extract"));

        let result = executor.execute(&mut runtime, &mut context);
        assert!(result.is_err());
        assert_eq!(runtime.called, vec!["extract", "cleanup"]);
    }

    #[test]
    fn continues_when_policy_is_continue() {
        let flow = flow_with_steps(
            r#"    - id: first
      type: emit_progress
      on_fail: continue
    - id: second
      type: emit_progress"#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        let mut runtime = MockRuntime::new(Some("first"));

        let report = executor
            .execute(&mut runtime, &mut context)
            .expect("continue should not fail");

        assert_eq!(runtime.called, vec!["first", "second"]);
        assert_eq!(report.continued_errors.len(), 1);
    }

    #[test]
    fn skips_step_when_condition_is_false() {
        let flow = flow_with_steps(
            r#"    - id: conditional
      type: emit_progress
      when: "${options.desktop_icons == true}"
    - id: always
      type: emit_progress"#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({ "desktop_icons": false });
        let mut runtime = MockRuntime::new(None);

        let report = executor
            .execute(&mut runtime, &mut context)
            .expect("execute");

        assert_eq!(runtime.called, vec!["always"]);
        assert_eq!(report.skipped_steps, vec!["conditional"]);
    }

    #[test]
    fn resolves_placeholders_in_params() {
        let flow = flow_with_steps(
            r#"    - id: extract
      type: extract_package
      params:
        target: "${InstallDir}"
        message: "Installing to ${InstallDir}""#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        let mut captured_target: Option<Value> = None;
        let mut captured_message: Option<Value> = None;

        struct CaptureRuntime<'a> {
            target: &'a mut Option<Value>,
            message: &'a mut Option<Value>,
        }

        impl FlowRuntime for CaptureRuntime<'_> {
            fn execute_builtin(
                &mut self,
                step: &FlowStep,
                _context: &mut FlowContext,
            ) -> Result<()> {
                let params = step.params_object();
                *self.target = params.get("target").cloned();
                *self.message = params.get("message").cloned();
                Ok(())
            }

            fn execute_script(
                &mut self,
                _step: &FlowStep,
                _context: &mut FlowContext,
            ) -> Result<()> {
                Ok(())
            }
        }

        let mut runtime = CaptureRuntime {
            target: &mut captured_target,
            message: &mut captured_message,
        };
        executor
            .execute(&mut runtime, &mut context)
            .expect("execute");

        assert_eq!(captured_target, Some(Value::String("C:\\Demo".to_string())));
        assert_eq!(
            captured_message,
            Some(Value::String("Installing to C:\\Demo".to_string()))
        );
    }

    #[test]
    fn supports_and_or_precedence_and_parentheses() {
        let flow = flow_with_steps(
            r#"    - id: without_parentheses
      type: emit_progress
      when: "${options.a == true || options.b == true && options.c == true}"
    - id: with_parentheses
      type: emit_progress
      when: "${(options.a == true || options.b == true) && options.c == true}""#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({
            "a": true,
            "b": false,
            "c": false
        });
        let mut runtime = MockRuntime::new(None);

        let report = executor
            .execute(&mut runtime, &mut context)
            .expect("execute");

        assert_eq!(runtime.called, vec!["without_parentheses"]);
        assert_eq!(report.skipped_steps, vec!["with_parentheses"]);
    }

    #[test]
    fn supports_numeric_comparisons() {
        let flow = flow_with_steps(
            r#"    - id: min_disk
      type: emit_progress
      when: "${options.disk_gb >= 20 && options.disk_gb < 100}""#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({ "disk_gb": 64 });
        let mut runtime = MockRuntime::new(None);

        executor
            .execute(&mut runtime, &mut context)
            .expect("execute");

        assert_eq!(runtime.called, vec!["min_disk"]);
    }

    #[test]
    fn invalid_expression_returns_error() {
        let flow = flow_with_steps(
            r#"    - id: broken
      type: emit_progress
      when: '${options.flag &&}'"#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({ "flag": true });
        let mut runtime = MockRuntime::new(None);

        let err = executor
            .execute(&mut runtime, &mut context)
            .expect_err("expression should fail");
        assert!(matches!(err, InstallerError::Config(_)));
    }

    #[test]
    fn supports_string_comparisons() {
        let flow = flow_with_steps(
            r#"    - id: string_check
      type: emit_progress
      when: '${options.channel == "stable" && options.locale != "en-US"}'"#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({
            "channel": "stable",
            "locale": "zh-CN"
        });
        let mut runtime = MockRuntime::new(None);

        executor
            .execute(&mut runtime, &mut context)
            .expect("execute");

        assert_eq!(runtime.called, vec!["string_check"]);
    }

    #[test]
    fn mixed_type_comparison_returns_error() {
        let flow = flow_with_steps(
            r#"    - id: mixed_type
      type: emit_progress
      when: "${options.disk_gb > options.label}""#,
            "    []",
        );
        let executor = FlowExecutor::new(flow).expect("executor");
        let mut context = FlowContext::from_definition(&executor.definition);
        context.options = serde_json::json!({
            "disk_gb": 64,
            "label": "large"
        });
        let mut runtime = MockRuntime::new(None);

        let err = executor
            .execute(&mut runtime, &mut context)
            .expect_err("mixed type comparison should fail");
        assert!(matches!(err, InstallerError::Config(_)));
    }
}
