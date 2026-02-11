// Return exit code 0 to allow install.
// The installer provides JSON context via env vars:
// MTI_ARGS_JSON, MTI_VARS_JSON, MTI_METADATA_JSON, MTI_OPTIONS_JSON.
const args = JSON.parse(process.env.MTI_ARGS_JSON || "{}");
const options = JSON.parse(process.env.MTI_OPTIONS_JSON || "{}");

if (!options.install_dir || typeof options.install_dir !== "string") {
  process.stderr.write("missing install_dir in options\n");
  process.exit(2);
}

if (typeof args.minimum_mb !== "number") {
  process.stderr.write("invalid minimum_mb argument\n");
  process.exit(3);
}

process.exit(0);
