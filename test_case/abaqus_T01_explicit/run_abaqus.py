#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
run_abaqus.py — 运行 Abaqus inp 文件，结束后清理中间产物，仅保留 .inp 与 .odb。

脚本行为：
  1. 接收 inp 文件路径，cd 到 inp 所在目录；
  2. 运行 `abaqus job=<name> int ask=off`（job 名默认取 inp 文件名 stem）；
  3. 运行成功后，删除该 job 产生的中间文件，仅保留 .inp 与 .odb；
  4. 运行失败时保留中间文件以便排查。

用法:
    python run_abaqus.py <input.inp> [--job NAME]

示例:
    python run_abaqus.py abaqus_T01.inp
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Abaqus 运行产生的中间文件后缀（将被清理）
ABAQUS_SCRATCH_EXTS = {
    '.abq', '.com', '.dat', '.mdl', '.msg', '.pac', '.prt',
    '.res', '.sel', '.sta', '.stt', '.log', '.rpy', '.rpyc',
    '.exception', '.SMABulk', '.SimConDB', '.sim',
}
# 保留的扩展名
KEEP_EXTS = {'.inp', '.odb'}


def _is_job_file(stem: str, job_lower: str) -> bool:
    """判断文件 stem 是否属于当前 job（含 Abaqus SIM 分区文件 _X1/_X2 等）。"""
    if stem == job_lower:
        return True
    # Abaqus SIM 分区文件命名: <job>_X1, <job>_X2, ...
    prefix = job_lower + '_x'
    return stem.startswith(prefix) and stem[len(prefix):].isdigit()


def cleanup(job_dir: Path, job: str):
    """删除 job 相关的 Abaqus 中间文件，仅保留 .inp/.odb。"""
    job_lower = job.lower()
    removed = []
    for f in job_dir.iterdir():
        if not f.is_file():
            continue
        if f.suffix.lower() in KEEP_EXTS:
            continue
        # 仅清理与当前 job 同名（大小写不敏感）的文件，含 _X1/_X2 分区文件
        if not _is_job_file(f.stem.lower(), job_lower):
            continue
        if f.suffix in ABAQUS_SCRATCH_EXTS:
            f.unlink()
            removed.append(f.name)
    return removed


def main(argv=None):
    ap = argparse.ArgumentParser(
        description='运行 Abaqus inp 并清理中间文件，仅保留 .inp 与 .odb。',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument('input', help='inp 文件路径')
    ap.add_argument('--job', default=None,
                    help='Abaqus job 名（默认取 inp 文件名 stem）')
    args = ap.parse_args(argv)

    if os.environ.get('NOVAFEA_SKIP_ABAQUS', '').strip().lower() in ('1', 'true', 'yes'):
        print("[√] 运行完成, NOVAFEA_SKIP_ABAQUS=1 已跳过 Abaqus 求解")
        return

    inp = Path(args.input).resolve()
    if not inp.exists():
        sys.exit(f"错误: 输入文件不存在: {inp}")

    job_dir = inp.parent
    job = args.job or inp.stem

    # 构造 abaqus 命令
    cmd = f'abaqus job={job} int ask=off'
    print(f"[*] 工作目录: {job_dir}")
    print(f"[*] 运行命令: {cmd}")
    print('-' * 60)

    env = os.environ.copy()
    ret = subprocess.run(cmd, cwd=str(job_dir), shell=True, env=env)

    print('-' * 60)
    if ret.returncode != 0:
        sys.exit(f"错误: Abaqus 运行失败（返回码 {ret.returncode}），已保留中间文件以便排查。")

    removed = cleanup(job_dir, job)
    summary = ', '.join(removed) if removed else '无'
    print(f"[√] 运行完成，已清理 {len(removed)} 个中间文件: {summary}")


if __name__ == '__main__':
    main()
