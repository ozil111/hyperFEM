from __future__ import annotations

import json
from typing import Any, Dict

try:
    from .fem_data_model import FEMDataModel
except ImportError:
    from fem_data_model import FEMDataModel


def load_control_into_model(control_file_path: str, model: FEMDataModel) -> None:
    """
    解析 control.json 并填充 FEMDataModel 的相关数据表。
    """
    with open(control_file_path, 'r', encoding='utf-8') as f:
        control_data: Dict[str, Any] = json.load(f)

    model.materials = control_data.get("Material", {}) or {}
    model.cross_sections = control_data.get("CrossSection", {}) or {}
    model.parts_properties = control_data.get("PartProperty", {}) or {}
    model.contacts = control_data.get("Contact", {}) or {}
    model.constraints = control_data.get("Constraint", {}) or {}
    model.loads = control_data.get("Load", {}) or {}
    # 可选：初始条件
    if hasattr(model, 'initial_conditions'):
        model.initial_conditions = control_data.get("InitialCondition", {}) or {}


