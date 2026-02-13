use crate::flow_executor::FlowContext;
use installer_shared::{FlowStep, InstallerError, Result};
use std::collections::HashMap;

mod base;
mod components;
mod system;

pub const STEP_CHECK_DISK: &str = "check_disk";
pub const STEP_EXTRACT_PACKAGE: &str = "extract_package";
pub const STEP_CREATE_SHORTCUT: &str = "create_shortcut";
pub const STEP_WRITE_REGISTRY: &str = "write_registry";
pub const STEP_CONFIGURE_AUTOSTART: &str = "configure_autostart";
pub const STEP_LOAD_COMPONENT_MANIFEST: &str = "load_component_manifest";
pub const STEP_RESOLVE_SELECTED_COMPONENTS: &str = "resolve_selected_components";
pub const STEP_PROCESS_SELECTED_COMPONENTS: &str = "process_selected_components";
pub const STEP_DOWNLOAD_COMPONENT: &str = "download_component";
pub const STEP_VERIFY_COMPONENT: &str = "verify_component";
pub const STEP_INSTALL_COMPONENT: &str = "install_component";
pub const STEP_ROLLBACK_COMPONENT: &str = "rollback_component";
pub const STEP_ROLLBACK_FILES: &str = "rollback_files";

type StepHandler = fn(&mut dyn BuiltinStepRuntime, &FlowStep, &mut FlowContext) -> Result<()>;

pub trait BuiltinStepRuntime {
    fn check_disk(&mut self) -> Result<()>;
    fn extract_package(&mut self) -> Result<()>;
    fn create_shortcut(&mut self) -> Result<()>;
    fn write_registry(&mut self) -> Result<()>;
    fn configure_autostart(&mut self) -> Result<()>;

    fn load_component_manifest(&mut self, step: &FlowStep) -> Result<()>;
    fn resolve_selected_components(
        &mut self,
        step: &FlowStep,
        context: &mut FlowContext,
    ) -> Result<()>;
    fn process_selected_components(
        &mut self,
        step: &FlowStep,
        context: &mut FlowContext,
    ) -> Result<()>;
    fn download_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()>;
    fn verify_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()>;
    fn install_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()>;
    fn rollback_component(&mut self) -> Result<()>;

    fn rollback_files(&mut self) -> Result<()>;
}

pub struct StepRegistry {
    handlers: HashMap<&'static str, StepHandler>,
}

impl StepRegistry {
    pub fn new() -> Self {
        let mut handlers = HashMap::new();
        base::register(&mut handlers);
        system::register(&mut handlers);
        components::register(&mut handlers);
        Self { handlers }
    }

    pub fn execute(
        &self,
        runtime: &mut dyn BuiltinStepRuntime,
        step: &FlowStep,
        context: &mut FlowContext,
    ) -> Result<()> {
        match self.handlers.get(step.step_type.as_str()) {
            Some(handler) => handler(runtime, step, context),
            None => Err(InstallerError::Config(format!(
                "Unsupported built-in flow step type '{}'",
                step.step_type
            ))),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[derive(Default)]
    struct MockRuntime;

    impl BuiltinStepRuntime for MockRuntime {
        fn check_disk(&mut self) -> Result<()> {
            Ok(())
        }
        fn extract_package(&mut self) -> Result<()> {
            Ok(())
        }
        fn create_shortcut(&mut self) -> Result<()> {
            Ok(())
        }
        fn write_registry(&mut self) -> Result<()> {
            Ok(())
        }
        fn configure_autostart(&mut self) -> Result<()> {
            Ok(())
        }
        fn load_component_manifest(&mut self, _step: &FlowStep) -> Result<()> {
            Ok(())
        }
        fn resolve_selected_components(
            &mut self,
            _step: &FlowStep,
            _context: &mut FlowContext,
        ) -> Result<()> {
            Ok(())
        }
        fn process_selected_components(
            &mut self,
            _step: &FlowStep,
            _context: &mut FlowContext,
        ) -> Result<()> {
            Ok(())
        }
        fn download_component(
            &mut self,
            _step: &FlowStep,
            _context: &mut FlowContext,
        ) -> Result<()> {
            Ok(())
        }
        fn verify_component(&mut self, _step: &FlowStep, _context: &mut FlowContext) -> Result<()> {
            Ok(())
        }
        fn install_component(
            &mut self,
            _step: &FlowStep,
            _context: &mut FlowContext,
        ) -> Result<()> {
            Ok(())
        }
        fn rollback_component(&mut self) -> Result<()> {
            Ok(())
        }
        fn rollback_files(&mut self) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn unknown_step_keeps_compatible_error_message() {
        let registry = StepRegistry::new();
        let mut runtime = MockRuntime;
        let mut context = FlowContext {
            vars: std::collections::HashMap::new(),
            metadata: json!({}),
            options: json!({}),
        };
        let step = FlowStep {
            id: "x".to_string(),
            step_type: "unknown_builtin".to_string(),
            params: json!({}),
            when: None,
            on_fail: None,
            engine: None,
        };

        let err = registry
            .execute(&mut runtime, &step, &mut context)
            .expect_err("expected unknown-step error");
        match err {
            InstallerError::Config(msg) => {
                assert!(msg.contains("Unsupported built-in flow step type 'unknown_builtin'"));
            }
            other => panic!("unexpected error type: {other}"),
        }
    }
}
