# DXL 0.1 semantic model source

YOLO11n-seg ONNX input, including the weights, used to build the bundled engine.
Ultralytics model license: AGPL-3.0. Upstream: https://github.com/ultralytics/ultralytics
Original weights: https://github.com/ultralytics/assets/releases/download/v8.3.0/yolo11n-seg.pt

Use TensorRT 11.2.1.2 and build_release_model.py (no VC flags; 2 GiB workspace).
See DEPENDENCIES.md for the complete setup and target-GPU limitations.
The engine's byte hash can vary with TensorRT tactic selection.

ONNX SHA256: 0bc32bc92e985b881141ef9bd2216e2a746f70519d0d24da9fc85decc4428cf4
Bundled engine SHA256: 3930db2f22938f0ff8eb835d4d0db6c78768a084ef990ce91738dbdbdccaa8e3
