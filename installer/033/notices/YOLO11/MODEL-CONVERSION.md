# YOLO11n-seg 模型格式说明

本包的人物识别模型（033-runtime/semantic/yolo11n-seg.onnx）来自 DXL 0.1 附带的 Ultralytics YOLO11n-seg ONNX 原件。
只用 ONNX 官方工具 onnx.version_converter（ONNX 1.19.1）把算子集从 opset 22 转成 21，没有重新训练、量化或裁剪；
355 个节点、223 个权重以及输入 / 输出定义逐字节保留。

- 原件 SHA256：0bc32bc92e985b881141ef9bd2216e2a746f70519d0d24da9fc85decc4428cf4
- 本包 SHA256：4fa0b870a6075e6e7fcb55fe5fe7cb10abc61c244a60fc2bbc563af28ee94f26

模型沿用 Ultralytics 的 AGPL-3.0 许可与署名（见同目录 LICENSE），来源链接见 MODEL-SOURCES.json。
