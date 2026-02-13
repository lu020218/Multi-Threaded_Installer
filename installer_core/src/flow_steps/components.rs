use crate::flow_executor::FlowContext;
use installer_shared::{FlowStep, Result};
use std::collections::HashMap;

use super::{
    BuiltinStepRuntime, StepHandler, STEP_DOWNLOAD_COMPONENT, STEP_INSTALL_COMPONENT,
    STEP_LOAD_COMPONENT_MANIFEST, STEP_PROCESS_SELECTED_COMPONENTS,
    STEP_RESOLVE_SELECTED_COMPONENTS, STEP_ROLLBACK_COMPONENT, STEP_VERIFY_COMPONENT,
};

pub fn register(handlers: &mut HashMap<&'static str, StepHandler>) {
    handlers.insert(STEP_LOAD_COMPONENT_MANIFEST, load_component_manifest);
    handlers.insert(
        STEP_RESOLVE_SELECTED_COMPONENTS,
        resolve_selected_components,
    );
    handlers.insert(
        STEP_PROCESS_SELECTED_COMPONENTS,
        process_selected_components,
    );
    handlers.insert(STEP_DOWNLOAD_COMPONENT, download_component);
    handlers.insert(STEP_VERIFY_COMPONENT, verify_component);
    handlers.insert(STEP_INSTALL_COMPONENT, install_component);
    handlers.insert(STEP_ROLLBACK_COMPONENT, rollback_component);
}

fn load_component_manifest(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.load_component_manifest(step)
}

fn resolve_selected_components(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    runtime.resolve_selected_components(step, context)
}

fn process_selected_components(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    runtime.process_selected_components(step, context)
}

fn download_component(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    runtime.download_component(step, context)
}

fn verify_component(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    runtime.verify_component(step, context)
}

fn install_component(
    runtime: &mut dyn BuiltinStepRuntime,
    step: &FlowStep,
    context: &mut FlowContext,
) -> Result<()> {
    runtime.install_component(step, context)
}

fn rollback_component(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.rollback_component()
}
