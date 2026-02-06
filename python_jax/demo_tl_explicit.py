import jax
import jax.numpy as jnp
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from mpl_toolkits.mplot3d import Axes3D  # 必须导入才能画 3D

# --- 1. 本构模型 ---
def neo_hookean_energy(F, mu, bulk):
    J = jnp.linalg.det(F)
    C = F.T @ F
    I1 = jnp.trace(C)
    # 增加一个极小的 epsilon 防止 J 刚好为 0 (虽然对负数无效，但在临界点有用)
    # 更好的做法是在求解器层检测 J < 0 并报错，但在 JAX scan 中较难中断
    energy = 0.5 * mu * (I1 - 3.0) - mu * jnp.log(J) + 0.5 * bulk * (J - 1.0) ** 2
    return energy

get_pk1_stress = jax.grad(neo_hookean_energy, argnums=0)

# --- 2. 单元内核 ---
def element_force_kernel(u_elem, X_elem, Dm_inv, vol, mu, bulk):
    x_elem = X_elem + u_elem
    Ds = (x_elem[1:] - x_elem[0]).T
    F = Ds @ Dm_inv
    P = get_pk1_stress(F, mu, bulk)
    H = vol * Dm_inv.T
    f_nodes_123 = P @ H
    f_node_0 = -jnp.sum(f_nodes_123, axis=1)
    f_local = jnp.vstack([f_node_0, f_nodes_123.T])
    return -f_local

# --- 3. 组装与时间积分 ---
@jax.jit
def explicit_step(state, _):
    # state 增加了 inv_mass_vec (预计算 1/m) 和 bc_mask
    (u, v, X, connectivity, Dm_inv, vols, inv_mass_vec, bc_mask, mu, bulk, step_dt) = state
    
    u_elems = u[connectivity]
    X_elems = X[connectivity]

    f_elems = jax.vmap(element_force_kernel, in_axes=(0, 0, 0, 0, None, None))(
        u_elems, X_elems, Dm_inv, vols, mu, bulk
    )

    global_forces = jnp.zeros_like(u)
    global_forces = global_forces.at[connectivity].add(f_elems)

    # F = ma => a = F * (1/m)
    a = global_forces * inv_mass_vec[:, None]
    
    # --- 关键修改：应用边界条件 ---
    # 将固定节点的加速度设为 0
    a = a * bc_mask
    
    v_new = v + a * step_dt
    # 将固定节点的速度也强制设为 0 (防止初速度漂移)
    v_new = v_new * bc_mask
    
    u_new = u + v_new * step_dt

    new_state = (u_new, v_new, X, connectivity, Dm_inv, vols, inv_mass_vec, bc_mask, mu, bulk, step_dt)
    
    return new_state, u_new

# --- 4. 前处理 (保持不变) ---
def build_single_tet_mesh():
    X = jnp.array([[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]])
    connectivity = jnp.array([[0, 1, 2, 3]])
    return X, connectivity

def preprocess_mesh(X, connectivity, rho=1.0):
    X_np = np.array(X)
    conn_np = np.array(connectivity)
    n_nodes = X_np.shape[0]
    n_elems = conn_np.shape[0]

    Dm_inv_list = []
    vol_list = []
    for e in range(n_elems):
        X_e = X_np[conn_np[e]]
        Dm = (X_e[1:] - X_e[0]).T
        vol = np.linalg.det(Dm) / 6.0
        Dm_inv_list.append(np.linalg.inv(Dm))
        vol_list.append(vol)

    Dm_inv = jnp.array(np.stack(Dm_inv_list))
    vols = jnp.array(vol_list)

    mass_vec = np.zeros(n_nodes)
    for e in range(n_elems):
        vol = float(vol_list[e])
        m_e = rho * vol
        for i in range(4):
            mass_vec[conn_np[e, i]] += m_e / 4.0
    
    # 预计算质量倒数，避免除法
    inv_mass_vec = jnp.array(1.0 / mass_vec)
    return Dm_inv, vols, inv_mass_vec

def run_simulation(initial_state, num_steps):
    final_state, trajectory = jax.lax.scan(
        explicit_step, initial_state, None, length=num_steps
    )
    return trajectory


def plot_results(trajectory, X_ref, dt, skip_step=500):
    """
    1. 画出节点 1 的 X 方向位移曲线
    2. 播放 3D 网格变形动画（当前位形 = X_ref + 位移）
    trajectory: (num_steps, n_nodes, 3) 位移
    X_ref: (n_nodes, 3) 参考构型坐标
    """
    trajectory = np.asarray(trajectory)
    X_ref = np.asarray(X_ref)
    num_steps, n_nodes, _ = trajectory.shape
    time = np.arange(num_steps) * dt

    # --- 1. 位移曲线 ---
    plt.figure(figsize=(10, 4))
    disp_x = trajectory[:, 1, 0]
    plt.plot(time, disp_x, label="Node 1 (X-displacement)")
    plt.axhline(0, color="k", linestyle="--", alpha=0.3)
    plt.title(f"Node 1 Oscillation (Max Disp: {disp_x.max():.4f})")
    plt.xlabel("Time (s)")
    plt.ylabel("Displacement X")
    plt.grid(True)
    plt.legend()
    plt.savefig("displacement_curve.png")
    print("Displacement curve saved to 'displacement_curve.png'")
    plt.close()

    # --- 2. 3D 动画 ---
    fig = plt.figure(figsize=(8, 6))
    ax = fig.add_subplot(111, projection="3d")
    frames = trajectory[::skip_step]
    n_frames = len(frames)

    ax.set_xlim(-0.2, 1.5)
    ax.set_ylim(-0.2, 1.2)
    ax.set_zlim(-0.2, 1.2)
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.set_title("Explicit Dynamics: Tet4 Oscillation")

    scat = ax.scatter([], [], [], c="b", s=50)
    scat_node1 = ax.scatter([], [], [], c="r", s=80, marker="*")
    edges = [[0, 1], [1, 2], [2, 0], [0, 3], [1, 3], [2, 3]]
    lines = [ax.plot([], [], [], "k-", alpha=0.5)[0] for _ in edges]

    def update(frame_idx):
        curr_X = X_ref + frames[frame_idx]
        current_time = frame_idx * skip_step * dt
        ax.set_title(f"Time: {current_time:.3f} s")
        scat._offsets3d = (curr_X[:, 0], curr_X[:, 1], curr_X[:, 2])
        scat_node1._offsets3d = ([curr_X[1, 0]], [curr_X[1, 1]], [curr_X[1, 2]])
        for line, edge in zip(lines, edges):
            p1, p2 = curr_X[edge[0]], curr_X[edge[1]]
            line.set_data([p1[0], p2[0]], [p1[1], p2[1]])
            line.set_3d_properties([p1[2], p2[2]])
        return [scat, scat_node1] + lines

    print(f"Generating animation with {n_frames} frames (skip_step={skip_step})...")
    ani = FuncAnimation(fig, update, frames=n_frames, interval=30, blit=False)
    plt.show()


def main():
    jax.config.update("jax_enable_x64", True)

    X, connectivity = build_single_tet_mesh()
    Dm_inv, vols, inv_mass_vec = preprocess_mesh(X, connectivity, rho=1.0)

    # --- 设置边界条件 Mask ---
    # 假设我们固定节点 0, 2, 3 (即 X=0 平面上的点)，只让节点 1 (X=1) 移动
    n_nodes = X.shape[0]
    # 1.0 代表自由度激活，0.0 代表固定
    bc_mask = jnp.ones((n_nodes, 3))
    
    # 固定节点 0, 2, 3 的所有方向
    fixed_nodes = jnp.array([0, 2, 3])
    bc_mask = bc_mask.at[fixed_nodes, :].set(0.0)

    mu, bulk = 1.0, 10.0
    u0 = jnp.zeros((n_nodes, 3))
    
    # 给节点 1 一个拉伸的初速度
    v0 = jnp.zeros((n_nodes, 3))
    v0 = v0.at[1].set(jnp.array([0.1, 0.0, 0.0])) # 向外拉

    dt = 1e-5
    
    # 状态中加入 inv_mass_vec 和 bc_mask
    state = (u0, v0, X, connectivity, Dm_inv, vols, inv_mass_vec, bc_mask, mu, bulk, dt)
    
    num_steps = 100_000  # dt=1e-5 -> 总时间 1.0
    trajectory = run_simulation(state, num_steps)

    print("Trajectory shape:", trajectory.shape)
    # 按步数采样输出节点 1 的 X 方向位移
    sample_steps = [0, num_steps // 4, num_steps // 2, num_steps * 3 // 4, num_steps - 1]
    for k in sample_steps:
        print(f"Node 1 displacement X at step {k}:", float(trajectory[k, 1, 0]))
    
    # 检查是否有 NaN
    if jnp.isnan(trajectory).any():
        print("Warning: Simulation exploded to NaN!")
    else:
        print("Simulation finished successfully.")

    # 可视化：转成 numpy 后绘图（从 GPU 拉回 CPU）
    trajectory_np = np.asarray(trajectory)
    X_np = np.asarray(X)
    print("Visualizing...")
    plot_results(trajectory_np, X_np, dt, skip_step=500)

if __name__ == "__main__":
    main()