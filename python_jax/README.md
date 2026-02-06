# JAX 极简显式求解器 Demo (Total Lagrangian)

基于 Total Lagrangian 格式的线性四面体 + 集总质量 + Neo-Hookean 超弹性显式动力学问答。

## 运行

```bash
pip install -r requirements.txt
python demo_tl_explicit.py
```

## 结构

- **本构**：`neo_hookean_energy` 只写应变能，PK1 由 `jax.grad` 自动得到。
- **单元**：`element_force_kernel` 处理单单元；`vmap` 并行到所有单元。
- **求解**：`explicit_step` 内 Gather → Compute → Scatter-Add → 中心差分；`jax.lax.scan` 跑整段仿真。
- **前处理**：`preprocess_mesh` 预计算 Dm_inv、vol、集总质量。

换本构只需改 `neo_hookean_energy`，其余不变；`jax.jit` 后 XLA 可生成高效 CPU/GPU 代码。
