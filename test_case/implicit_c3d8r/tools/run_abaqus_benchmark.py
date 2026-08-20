#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""运行 Abaqus 原生 C3D8R 求解并提取 benchmark CSV。

用法:
    python run_abaqus_benchmark.py <inp_file>

工作流程:
  1. cd 到 inp 所在目录, 运行 `abaqus job=<stem> int ask=off`
  2. 提取位移 CSV -> <stem>_abaqus_disp.csv
  3. 提取单元场 CSV -> <stem>_abaqus_elements.csv
  4. 清理 Abaqus 中间文件, 仅保留 .inp 和 .odb

输出的 benchmark CSV 格式与 NovaFEA --output-csv 目标一致:
  - _disp.csv:  instance,node_label,u1,u2,u3,mag
  - _elements.csv: variable,component,location,instance,element_label,node_label,integration_point,value
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Abaqus 运行产生的中间文件后缀 (将被清理)
ABAQUS_SCRATCH_EXTS = {
    '.abq', '.com', '.dat', '.mdl', '.msg', '.pac', '.prt',
    '.res', '.sel', '.sta', '.stt', '.log', '.rpy', '.rpyc',
    '.exception', '.SMABulk', '.SimConDB', '.sim',
}
KEEP_EXTS = {'.inp', '.odb'}


def _is_job_file(stem, job_lower):
    if stem == job_lower:
        return True
    prefix = job_lower + '_x'
    return stem.startswith(prefix) and stem[len(prefix):].isdigit()


def cleanup(job_dir, job):
    """删除 job 相关的 Abaqus 中间文件, 仅保留 .inp/.odb。"""
    job_lower = job.lower()
    removed = []
    for f in Path(job_dir).iterdir():
        if not f.is_file():
            continue
        if f.suffix.lower() in KEEP_EXTS:
            continue
        if not _is_job_file(f.stem.lower(), job_lower):
            continue
        if f.suffix in ABAQUS_SCRATCH_EXTS:
            f.unlink()
            removed.append(f.name)
    return removed


def main(argv=None):
    ap = argparse.ArgumentParser(description='Abaqus benchmark 生成器')
    ap.add_argument('inp', help='inp 文件路径')
    ap.add_argument('--job', default=None, help='Abaqus job 名 (默认取 inp 文件名 stem)')
    args = ap.parse_args(argv)

    if os.environ.get('NOVAFEA_SKIP_ABAQUS', '').strip().lower() in ('1', 'true', 'yes'):
        print("[√] 运行完成, NOVAFEA_SKIP_ABAQUS=1 已跳过 Abaqus 基准生成 (沿用仓库内 benchmark CSV)")
        return

    inp = Path(args.inp).resolve()
    if not inp.exists():
        sys.exit(f"错误: 输入文件不存在: {inp}")

    job_dir = inp.parent
    job = args.job or inp.stem

    # ---- 定位 extractODB 脚本 ----
    # 相对于本脚本: tools/ -> extern/vuel/extractODB/
    script_dir = Path(__file__).resolve().parent.parent.parent.parent / 'extern' / 'vuel' / 'extractODB'
    extract_disp = script_dir / 'extract_displacement_csv.py'
    extract_elem = script_dir / 'extract_validation_csv.py'

    # ---- Step 1: 运行 Abaqus (标准 C3D8R, 不用 VUEL) ----
    cmd = f'abaqus job={job} int ask=off'
    print(f"[*] 工作目录: {job_dir}")
    print(f"[*] 运行命令: {cmd}")
    print('-' * 60)
    ret = subprocess.run(cmd, cwd=str(job_dir), shell=True)
    print('-' * 60)
    if ret.returncode != 0:
        sys.exit(f"错误: Abaqus 运行失败 (返回码 {ret.returncode})")

    odb_file = job_dir / f'{job}.odb'
    if not odb_file.exists():
        sys.exit(f"错误: ODB 未生成: {odb_file}")

    # ---- Step 2: 提取位移 CSV ----
    disp_csv = job_dir / f'{job}_abaqus_disp.csv'
    if extract_disp.exists():
        ext_cmd = f'abaqus python "{extract_disp}" "{odb_file}" "{disp_csv}"'
        print(f"[*] 提取位移: {ext_cmd}")
        print('-' * 60)
        ext_ret = subprocess.run(ext_cmd, cwd=str(job_dir), shell=True)
        print('-' * 60)
        if ext_ret.returncode != 0:
            print(f"[!] 警告: 位移提取失败 (返回码 {ext_ret.returncode})")
    else:
        print(f"[!] 跳过位移提取: 脚本不存在 ({extract_disp})")

    # ---- Step 3: 提取单元场 CSV (S, E) ----
    elem_prefix = job_dir / f'{job}_abaqus'
    if extract_elem.exists():
        ext_cmd = f'abaqus python "{extract_elem}" "{odb_file}" --prefix "{elem_prefix}"'
        print(f"[*] 提取单元场: {ext_cmd}")
        print('-' * 60)
        ext_ret = subprocess.run(ext_cmd, cwd=str(job_dir), shell=True)
        print('-' * 60)
        if ext_ret.returncode != 0:
            print(f"[!] 警告: 单元场提取失败 (返回码 {ext_ret.returncode})")
    else:
        print(f"[!] 跳过单元场提取: 脚本不存在 ({extract_elem})")

    # ---- Step 4: 清理 ----
    removed = cleanup(job_dir, job)
    summary = ', '.join(removed) if removed else '无'
    print(f"[√] 运行完成, 已清理 {len(removed)} 个中间文件: {summary}")


if __name__ == '__main__':
    main()
