use crate::flow_executor::FlowContext;
use installer_shared::{FlowStep, Result};
use std::collections::HashMap;

use super::{
    BuiltinStepRuntime, StepHandler, STEP_CHECK_DISK, STEP_EXTRACT_PACKAGE, STEP_ROLLBACK_FILES,
};

pub fn register(handlers: &mut HashMap<&'static str, StepHandler>) {
    handlers.insert(STEP_CHECK_DISK, check_disk);
    handlers.insert(STEP_EXTRACT_PACKAGE, extract_package);
    handlers.insert(STEP_ROLLBACK_FILES, rollback_files);
}

fn check_disk(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.check_disk()
}

fn extract_package(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.extract_package()
}

fn rollback_files(
    runtime: &mut dyn BuiltinStepRuntime,
    _step: &FlowStep,
    _context: &mut FlowContext,
) -> Result<()> {
    runtime.rollback_files()
}
