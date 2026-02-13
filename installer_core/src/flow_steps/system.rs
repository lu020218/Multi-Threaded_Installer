use crate::flow_executor::FlowContext;
use installer_shared::{FlowStep, Result};
use std::collections::HashMap;

use super::{
    BuiltinStepRuntime, StepHandler, STEP_CONFIGURE_AUTOSTART, STEP_CREATE_SHORTCUT,
    STEP_WRITE_REGISTRY,
};

pub fn register(handlers: &mut HashMap<&'static str, StepHandler>) {
    handlers.insert(STEP_CREATE_SHORTCUT, create_shortcut);
    handlers.insert(STEP_WRITE_REGISTRY, write_registry);
    handlers.insert(STEP_CONFIGURE_AUTOSTART, configure_autostart);
}

fn create_shortcut(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.create_shortcut()
}

fn write_registry(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.write_registry()
}

fn configure_autostart(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.configure_autostart()
}
