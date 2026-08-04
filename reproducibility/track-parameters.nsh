# TRACK project-level parameters only.
# Aircraft-specific calibration, battery, RC endpoints and actuator settings
# must be configured for each aircraft in QGroundControl.

# This reproduces the tested three-position mode-switch mapping.
# Change COM_FLTMODE6 if Track is assigned to another switch position.
param set COM_FLTMODE6 16

# Explicitly record the tested TRACK defaults.
param set TRK_TGT_TIMEOUT 0.30
param set TRK_LOST_DELAY 0.80
param set TRK_CONF_MIN 0.50
param set TRK_TWIST_MAX 90.0
param set TRK_TWIST_DZ 0.05
param set TRK_DIR_TC 0.08
param set TRK_ATT_RATE_MAX 120.0
param set TRK_TILT_MAX 60.0
param set TRK_ENTRY_T 0.50
param set TRK_LOST_ACT 0

param save
